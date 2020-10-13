/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "debug/data_dump/e2e_dump_util.h"
#include <algorithm>
#include "debug/data_dump/dump_json_parser.h"
#include "common/trans.h"
#include "backend/session/anf_runtime_algorithm.h"
#include "utils/ms_context.h"
#ifdef ENABLE_DEBUGGER
#include "debug/debug_services.h"
#include "debug/tensor_load.h"
#include "debug/debugger/debugger.h"
#endif

namespace {
const size_t PRAMATER_OUTPUT_INDEX = 0;
const size_t VALUE_NODE_OUTPUT_INDEX = 0;
}  // namespace

namespace mindspore {
void E2eDumpUtil::GetFileKernelName(NotNull<std::string *> kernel_name) {
  const std::string strsrc = "/";
  const std::string strdst = "--";
  std::string::size_type pos = 0;
  std::string::size_type srclen = strsrc.size();
  std::string::size_type dstlen = strdst.size();
  while ((pos = kernel_name->find(strsrc, pos)) != std::string::npos) {
    kernel_name->replace(pos, srclen, strdst);
    pos += dstlen;
  }
}

bool E2eDumpUtil::IsDeviceTargetGPU() {
  auto context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context);
  return context->get_param<std::string>(MS_CTX_DEVICE_TARGET) == kGPUDevice;
}

void E2eDumpUtil::DumpMemToFile(const std::string &file_path, NotNull<const device::DeviceAddress *> addr,
                                bool trans_flag, const ShapeVector &int_shapes, const TypeId &type) {
  auto format = kOpFormat_DEFAULT;
  auto ret = addr->DumpMemToFile(trans_flag, file_path, format, int_shapes, type);
  if (!ret) {
    MS_LOG(ERROR) << "DumpMemToFile Failed: flag:" << trans_flag << ", path:" << file_path << ", host_format:" << format
                  << ".!";
  }
}

void E2eDumpUtil::DumpGPUMemToFile(const std::string &file_path, const std::string &original_kernel_name,
                                   NotNull<const device::DeviceAddress *> addr, bool trans_flag,
                                   const ShapeVector &int_shapes, const TypeId &type, size_t slot, Debugger *debugger) {
#ifdef ENABLE_DEBUGGER
  auto format = kOpFormat_DEFAULT;
  MS_EXCEPTION_IF_NULL(debugger);
  DebugServices *debug_services = debugger->debug_services();
  MS_EXCEPTION_IF_NULL(debug_services);
  TensorLoader *tensor_loader = debug_services->tensor_loader();
  MS_EXCEPTION_IF_NULL(tensor_loader);
  auto ret = tensor_loader->DumpTensorToFile(original_kernel_name, trans_flag, file_path, format, int_shapes, type,
                                             addr->type_id(), addr->format(), slot);
  if (!ret) {
    MS_LOG(ERROR) << "DumpTensorToFile Failed: flag:" << std::to_string(trans_flag) << ", path:" << file_path
                  << ", host_format:" << format;
  }
#endif
}

void E2eDumpUtil::GetDumpIntShape(const AnfNodePtr &node, size_t index, bool trans_flag,
                                  NotNull<ShapeVector *> int_shapes) {
  if (trans_flag) {
    *int_shapes = trans::GetRuntimePaddingShape(node, index);
  } else {
    auto shape = AnfAlgo::GetOutputDeviceShape(node, index);
    (void)std::transform(shape.begin(), shape.end(), std::back_inserter(*int_shapes),
                         [](size_t inner_item) { return SizeToInt(inner_item); });
  }
}

void E2eDumpUtil::DumpOutput(const session::KernelGraph *graph, const std::string &dump_path, Debugger *debugger) {
  MS_EXCEPTION_IF_NULL(graph);
  auto &dump_json_parser = DumpJsonParser::GetInstance();
  if (!dump_json_parser.OutputNeedDump()) {
    return;
  }
  MS_LOG(INFO) << "Start e2e dump output";
  bool trans_flag = dump_json_parser.trans_flag();
  const auto &apply_kernels = graph->execution_order();
  for (const auto &node : apply_kernels) {
    MS_EXCEPTION_IF_NULL(node);
    auto node_name = AnfAlgo::GetCNodeName(node);
    std::string kernel_name = node->fullname_with_scope();
    if (!dump_json_parser.NeedDump(kernel_name)) {
      continue;
    }
    DumpJsonParser::GetInstance().MatchKernel(kernel_name);
    GetFileKernelName(NOT_NULL(&kernel_name));
    auto output_size = AnfAlgo::GetOutputTensorNum(node);
    for (size_t j = 0; j < output_size; ++j) {
      auto addr = AnfAlgo::GetOutputAddr(node, j);
      ShapeVector int_shapes;
      GetDumpIntShape(node, j, trans_flag, NOT_NULL(&int_shapes));
      auto type = AnfAlgo::GetOutputInferDataType(node, j);
      std::string file_path = dump_path + '/' + kernel_name + '_' + "output_" + std::to_string(j);
      if (IsDeviceTargetGPU()) {
        DumpGPUMemToFile(file_path, node->fullname_with_scope(), NOT_NULL(addr), trans_flag, int_shapes, type, j,
                         debugger);
      } else {
        DumpMemToFile(file_path, NOT_NULL(addr), trans_flag, int_shapes, type);
      }
    }
  }
}

void E2eDumpUtil::DumpInput(const session::KernelGraph *graph, const std::string &dump_path, Debugger *debugger) {
  MS_EXCEPTION_IF_NULL(graph);
  auto &dump_json_parser = DumpJsonParser::GetInstance();
  if (!dump_json_parser.InputNeedDump()) {
    return;
  }
  MS_LOG(INFO) << "Start e2e dump input";
  bool trans_flag = dump_json_parser.trans_flag();
  const auto &apply_kernels = graph->execution_order();
  for (const auto &node : apply_kernels) {
    MS_EXCEPTION_IF_NULL(node);
    auto node_name = AnfAlgo::GetCNodeName(node);
    std::string kernel_name = node->fullname_with_scope();
    if (!dump_json_parser.NeedDump(kernel_name)) {
      continue;
    }
    DumpJsonParser::GetInstance().MatchKernel(kernel_name);
    GetFileKernelName(NOT_NULL(&kernel_name));
    auto input_size = AnfAlgo::GetInputTensorNum(node);
    for (size_t j = 0; j < input_size; ++j) {
      auto kernel_with_index = AnfAlgo::GetPrevNodeOutput(node, j);
      auto input = kernel_with_index.first;
      auto index = kernel_with_index.second;
      auto addr = AnfAlgo::GetOutputAddr(input, index);

      ShapeVector int_shapes;
      GetDumpIntShape(input, index, trans_flag, NOT_NULL(&int_shapes));
      auto type = AnfAlgo::GetOutputInferDataType(input, index);
      std::string file_path = dump_path + '/' + kernel_name + '_' + "input_" + std::to_string(j);
      if (IsDeviceTargetGPU()) {
        DumpGPUMemToFile(file_path, node->fullname_with_scope(), NOT_NULL(addr), trans_flag, int_shapes, type, j,
                         debugger);
      } else {
        DumpMemToFile(file_path, NOT_NULL(addr), trans_flag, int_shapes, type);
      }
    }
  }
}

void E2eDumpUtil::DumpSingleAnfnode(const AnfNodePtr &anf_node, const size_t output_index, const std::string &dump_path,
                                    bool trans_flag, Debugger *debugger) {
  MS_EXCEPTION_IF_NULL(anf_node);
  auto &dump_json_parser = DumpJsonParser::GetInstance();
  if (!anf_node->isa<Parameter>() && !anf_node->isa<ValueNode>()) {
    return;
  }
  std::string node_name = anf_node->fullname_with_scope();
  if (!dump_json_parser.NeedDump(node_name)) {
    return;
  }
  DumpJsonParser::GetInstance().MatchKernel(node_name);
  GetFileKernelName(NOT_NULL(&node_name));
  // check if output adde exists, if not, return;
  if (!AnfAlgo::OutputAddrExist(anf_node, output_index)) {
    return;
  }
  auto addr = AnfAlgo::GetOutputAddr(anf_node, output_index);
  MS_EXCEPTION_IF_NULL(addr);
  ShapeVector int_shapes;
  GetDumpIntShape(anf_node, output_index, trans_flag, NOT_NULL(&int_shapes));
  auto type = AnfAlgo::GetOutputInferDataType(anf_node, output_index);

  std::string file_path = dump_path + '/' + node_name + '_' + "output_0";
  if (IsDeviceTargetGPU()) {
    DumpGPUMemToFile(file_path, node_name, NOT_NULL(addr), trans_flag, int_shapes, type, 0, debugger);
  } else {
    DumpMemToFile(file_path, NOT_NULL(addr), trans_flag, int_shapes, type);
  }
}

void E2eDumpUtil::DumpParametersAndConst(const session::KernelGraph *graph, const std::string &dump_path,
                                         Debugger *debugger) {
  MS_EXCEPTION_IF_NULL(graph);
  auto &dump_json_parser = DumpJsonParser::GetInstance();
  MS_LOG(INFO) << "Start e2e dump parameters and Const values";
  bool trans_flag = dump_json_parser.trans_flag();
  // dump parameters
  const auto &parameters = graph->inputs();
  for (auto &item : parameters) {
    DumpSingleAnfnode(item, PRAMATER_OUTPUT_INDEX, dump_path, trans_flag, debugger);
  }
  // dump const values
  auto value_nodes = graph->graph_value_nodes();
  for (const auto &value_node : value_nodes) {
    DumpSingleAnfnode(value_node, VALUE_NODE_OUTPUT_INDEX, dump_path, trans_flag, debugger);
  }
}

bool E2eDumpUtil::DumpData(const session::KernelGraph *graph, uint32_t device_id, Debugger *debugger) {
  MS_EXCEPTION_IF_NULL(graph);
  auto &dump_json_parser = DumpJsonParser::GetInstance();
  dump_json_parser.UpdateDumpIter();
  auto dump_flag = dump_json_parser.e2e_dump_enabled();
  if (!dump_flag) {
    MS_LOG(INFO) << "E2e dump is disabled, skip dump step";
    return true;
  }

  if (dump_json_parser.iteration() != 0) {
    if (dump_json_parser.cur_dump_iter() != dump_json_parser.iteration()) {
      return true;
    }
  }
  MS_LOG(INFO) << "Start e2e dump. Current iteration is " << dump_json_parser.cur_dump_iter();

  std::string net_name = dump_json_parser.net_name();
  std::string iterator = std::to_string(dump_json_parser.cur_dump_iter());
  std::string dump_path = dump_json_parser.path();
  if (dump_path.back() != '/') {
    dump_path += "/";
  }
  dump_path += (net_name + "/device_" + std::to_string(device_id) + "/iteration_" + iterator);
  DumpInput(graph, dump_path, debugger);
  DumpOutput(graph, dump_path, debugger);
  DumpParametersAndConst(graph, dump_path, debugger);
  return true;
}
}  // namespace mindspore
