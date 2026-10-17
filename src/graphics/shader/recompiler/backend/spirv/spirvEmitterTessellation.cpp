#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h"

#include <algorithm>
#include <optional>

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {
namespace {

uint32_t TessellationPointer(ValueEmitContext& ctx, const IR::Inst& inst) {
	auto& state          = ctx.state;
	using Attribute      = IR::TessellationAttribute;
	const auto  kind     = static_cast<Attribute>(inst.Arg(0).U32());
	const auto& tess     = state.input_info.vertex->tess;
	const auto  variable = state.tess_variables.at(static_cast<uint32_t>(kind));
	EXIT_IF(variable == 0);
	const auto input = kind == Attribute::ControlInput || kind == Attribute::EvaluationInput ||
	                   (kind == Attribute::PatchOutput &&
	                    state.program.stage == ShaderType::TessellationEvaluation);
	const auto storage = input ? spv::StorageClassInput : spv::StorageClassOutput;
	const auto pointer = state.builder.AllocateId();
	if (kind == Attribute::Factor) {
		uint32_t outer_count = 3u;
		if (tess.domain == 0u) {
			outer_count = 2u;
		} else if (tess.domain == 2u) {
			outer_count = 4u;
		}
		std::optional<uint32_t> constant_byte_offset;
		const auto resolve_offset = [&](auto& self, const IR::Value& val) -> void {
			const auto resolved = val.Resolve();
			if (resolved.IsImmediate()) {
				constant_byte_offset = (constant_byte_offset.value_or(0u)) + resolved.U32();
				return;
			}
			const auto* src_inst = resolved.TryInstruction();
			if (src_inst != nullptr) {
				const auto op = src_inst->GetOpcode();
				if (op == IR::ValueOpcode::IAdd32) {
					self(self, src_inst->Arg(0));
					self(self, src_inst->Arg(1));
				} else if (op == IR::ValueOpcode::ISub32) {
					if (src_inst->Arg(1).Resolve().IsImmediate()) {
						constant_byte_offset = (constant_byte_offset.value_or(0u)) - src_inst->Arg(1).Resolve().U32();
					}
					self(self, src_inst->Arg(0));
				} else if (op == IR::ValueOpcode::SelectU32) {
					self(self, src_inst->Arg(1));
				}
			}
		};
		resolve_offset(resolve_offset, inst.Arg(1));
		const auto type = TypePointer(state, storage, TypeF32(state));
		if (constant_byte_offset.has_value()) {
			const auto raw_index    = *constant_byte_offset / 4u;
			const auto max_factors  = outer_count + (tess.domain == 2u ? 2u : (tess.domain == 1u ? 1u : 0u));
			const auto index        = max_factors > 0 ? (raw_index % max_factors) : 0u;
			const bool outer        = index < outer_count;
			const auto target_index = outer ? index : (index - outer_count);
			state.builder.AddFunction(spv::OpAccessChain, type,
			                          pointer, outer ? variable : state.tess_inner_variable,
			                          ConstantU32(state, target_index));
			return pointer;
		}

		state.builder.AddFunction(spv::OpAccessChain, type, pointer, variable, ConstantU32(state, 0));
		return pointer;
	}
	auto address = ctx.Arg(inst, 1);
	if (kind == Attribute::PatchOutput) {
		address =
		    EmitBinaryU32(state, spv::OpISub, address, ConstantU32(state, state.tess_patch_base));
	}
	const bool local  = kind == Attribute::LocalOutput || kind == Attribute::ControlInput;
	const auto stride = local ? tess.ls_stride : tess.hs_stride;
	const auto offset = kind == Attribute::PatchOutput ? address
	                                                   : EmitBinaryU32(state, spv::OpUMod, address,
	                                                                   ConstantU32(state, stride));
	const auto attribute =
	    EmitBinaryU32(state, spv::OpShiftRightLogical, offset, ConstantU32(state, 4));
	const auto component =
	    EmitBinaryU32(state, spv::OpBitwiseAnd,
	                  EmitBinaryU32(state, spv::OpShiftRightLogical, offset, ConstantU32(state, 2)),
	                  ConstantU32(state, 3));
	const auto type = TypePointer(state, storage, TypeU32(state));
	if (kind == Attribute::LocalOutput || kind == Attribute::PatchOutput) {
		state.builder.AddFunction(spv::OpAccessChain, type, pointer, variable, attribute,
		                          component);
	} else {
		const auto vertex =
		    kind == Attribute::ControlOutput
		        ? EmitLaneId(state)
		        : EmitBinaryU32(state, spv::OpUDiv, address, ConstantU32(state, stride));
		state.builder.AddFunction(spv::OpAccessChain, type, pointer, variable, vertex, attribute,
		                          component);
	}
	return pointer;
}

} // namespace

void DefineTessellationInterfaces(EmitterState& state) {
	using Attribute = IR::TessellationAttribute;
	std::array<bool, 6> used {};
	uint32_t            patch_begin = UINT32_MAX, patch_end = 0;
	for (const auto* block: state.program.blocks) {
		for (const auto& inst: *block) {
			if (inst.GetOpcode() != IR::ValueOpcode::GetTessellationAttribute &&
			    inst.GetOpcode() != IR::ValueOpcode::SetTessellationAttribute)
				continue;
			const auto kind = inst.Arg(0).U32();
			used.at(kind)   = true;
			if (kind == static_cast<uint32_t>(Attribute::PatchOutput)) {
				uint32_t patch_offset = 0;
				const auto patch_arg = inst.Arg(1).Resolve();
				if (patch_arg.IsImmediate()) {
					patch_offset = patch_arg.U32();
				} else if (const auto* src_i = patch_arg.TryInstruction();
				           src_i != nullptr && src_i->GetOpcode() == IR::ValueOpcode::IAdd32) {
					if (src_i->Arg(0).Resolve().IsImmediate() && src_i->Arg(1).Resolve().IsImmediate()) {
						patch_offset = src_i->Arg(0).Resolve().U32() + src_i->Arg(1).Resolve().U32();
					} else if (src_i->Arg(0).Resolve().IsImmediate()) {
						patch_offset = src_i->Arg(0).Resolve().U32();
					} else if (src_i->Arg(1).Resolve().IsImmediate()) {
						patch_offset = src_i->Arg(1).Resolve().U32();
					}
				}
				patch_begin = std::min(patch_begin, patch_offset);
				patch_end   = std::max(patch_end, patch_offset + 4u);
			}
		}
	}
	if (std::ranges::none_of(used, [](bool value) { return value; })) return;
	const auto& tess  = state.input_info.vertex->tess;
	const auto  array = [&](uint32_t type, uint32_t count) {
		return state.builder.Type(spv::OpTypeArray, type, ConstantU32(state, count));
	};
	for (uint32_t index = 0; index < used.size(); index++) {
		if (!used[index]) continue;
		const auto kind    = static_cast<Attribute>(index);
		const bool input   = kind == Attribute::ControlInput || kind == Attribute::EvaluationInput ||
		                   (kind == Attribute::PatchOutput &&
		                    state.program.stage == ShaderType::TessellationEvaluation);
		const auto storage = input ? spv::StorageClassInput : spv::StorageClassOutput;
		uint32_t   type;
		if (kind == Attribute::Factor) {
			type = array(TypeF32(state), 4u);
		} else if (kind == Attribute::PatchOutput) {
			state.tess_patch_base = patch_begin != UINT32_MAX ? patch_begin : 0u;
			const auto span = (patch_end > state.tess_patch_base)
			                      ? (patch_end - state.tess_patch_base) : 16u;
			type = array(TypeU32Vector(state, 4), (span + 15u) / 16u);
		} else {
			const bool local  = kind == Attribute::LocalOutput || kind == Attribute::ControlInput;
			const auto stride = local ? tess.ls_stride : tess.hs_stride;
			type              = array(TypeU32Vector(state, 4), (stride + 15u) / 16u);
			if (kind != Attribute::LocalOutput) {
				type = array(type, local ? tess.input_control_points : tess.output_control_points);
			}
		}
		auto& variable = state.tess_variables[index];
		variable       = DefineInterfaceVariable(state, type, storage, "tess_attributes");
		if (kind == Attribute::Factor) {
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBuiltIn,
			                            spv::BuiltInTessLevelOuter);
			state.tess_inner_variable =
			    DefineInterfaceVariable(state, array(TypeF32(state), 2u), storage, "tess_inner");
			state.builder.AddAnnotation(spv::OpDecorate, state.tess_inner_variable,
			                            spv::DecorationBuiltIn, spv::BuiltInTessLevelInner);
			state.builder.AddAnnotation(spv::OpDecorate, state.tess_inner_variable,
			                            spv::DecorationPatch);
		} else {
			state.builder.AddAnnotation(
			    spv::OpDecorate, variable, spv::DecorationLocation,
			    kind == Attribute::PatchOutput ? (tess.hs_stride + 15u) / 16u : 0u);
		}
		if (kind == Attribute::Factor || kind == Attribute::PatchOutput) {
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationPatch);
		}
	}
}

void DefineTessellationExecutionModes(EmitterState& state) {
	const auto& tess = state.input_info.vertex->tess;
	state.builder.RequireCapability(spv::CapabilityTessellation);
	if (state.program.stage == ShaderType::TessellationControl) {
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeOutputVertices,
		                               tess.output_control_points);
	} else {
		switch (tess.domain) {
			case 0u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeIsolines); break;
			case 1u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeTriangles); break;
			case 2u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeQuads); break;
			default: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeTriangles); break;
		}
		switch (tess.partitioning) {
			case 0u:
			case 1u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSpacingEqual); break;
			case 2u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSpacingFractionalOdd); break;
			case 3u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSpacingFractionalEven); break;
			default: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSpacingFractionalOdd); break;
		}
		switch (tess.output_topology) {
			case 2u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeVertexOrderCw); break;
			case 3u: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeVertexOrderCcw); break;
			default: state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeVertexOrderCw); break;
		}
	}
}

uint32_t EmitGetTessellationAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	return EmitValueOrZeroIfCondition(ctx.state, ctx.Arg(inst, 2), [&] {
		const auto value = ctx.state.builder.AllocateId();
		ctx.state.builder.AddFunction(spv::OpLoad, TypeU32(ctx.state), value,
		                              TessellationPointer(ctx, inst));
		return value;
	});
}

void EmitSetTessellationAttribute(ValueEmitContext& ctx, const IR::Inst& inst) {
	EmitIfCondition(ctx.state, ctx.Arg(inst, 3), [&] {
		auto value = ctx.Arg(inst, 2);
		if (inst.Arg(0).U32() == static_cast<uint32_t>(IR::TessellationAttribute::Factor)) {
			const auto floating = ctx.state.builder.AllocateId();
			ctx.state.builder.AddFunction(spv::OpBitcast, TypeF32(ctx.state), floating, value);
			value = floating;
		}
		ctx.state.builder.AddFunction(spv::OpStore, TessellationPointer(ctx, inst), value);
	});
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
