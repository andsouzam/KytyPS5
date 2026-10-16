#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h"

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {

uint32_t EmitAndConstant(EmitterState& state, uint32_t value, uint32_t mask) {
	const auto ret = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpBitwiseAnd, TypeU32(state), ret, value,
	                          ConstantU32(state, mask));
	return ret;
}

uint32_t EmitCompareU32Constant(EmitterState& state, spv::Op opcode, uint32_t value,
                                uint32_t constant) {
	const auto ret = state.builder.AllocateId();
	state.builder.AddFunction(opcode, TypeBool(state), ret, value, ConstantU32(state, constant));
	return ret;
}

uint32_t EmitMinMaxU32Value(EmitterState& state, uint32_t lhs, uint32_t rhs, bool max_value) {
	const auto cond = state.builder.AllocateId();
	const auto ret  = state.builder.AllocateId();
	state.builder.AddFunction(max_value ? spv::OpUGreaterThan : spv::OpULessThan, TypeBool(state),
	                          cond, lhs, rhs);
	state.builder.AddFunction(spv::OpSelect, TypeU32(state), ret, cond, lhs, rhs);
	return ret;
}

uint32_t EmitMinMaxI32Value(EmitterState& state, uint32_t lhs, uint32_t rhs, bool max_value) {
	const auto cond = state.builder.AllocateId();
	const auto ret  = state.builder.AllocateId();
	state.builder.AddFunction(max_value ? spv::OpSGreaterThan : spv::OpSLessThan, TypeBool(state),
	                          cond, lhs, rhs);
	state.builder.AddFunction(spv::OpSelect, TypeU32(state), ret, cond, lhs, rhs);
	return ret;
}

// OpIsNan holds for every NaN encoding, signaling ones included, and is one comparison where the
// exponent and mantissa tests took five instructions: the module preserves NaN
// (SignedZeroInfNanPreserve), so the driver may not fold it away.
static F32Class EmitClassifyF32Value(EmitterState& state, uint32_t value, uint32_t bits) {
	F32Class cls;
	cls.bits = bits;
	cls.nan  = Unary(state, spv::OpIsNan, TypeBool(state), value);
	cls.zero = EmitCompareU32Constant(state, spv::OpIEqual, EmitAndConstant(state, bits, 0x7fffffffu),
	                                  0);
	return cls;
}

F32Class EmitClassifyF32Bits(EmitterState& state, uint32_t bits) {
	return EmitClassifyF32Value(state, EmitBitcastU32ToF32(state, bits), bits);
}

F32Class EmitClassifyF32(EmitterState& state, uint32_t value) {
	return EmitClassifyF32Value(state, value, EmitBitcastF32ToU32(state, value));
}

uint32_t EmitClassMaskBitMatch(EmitterState& state, uint32_t mask, uint32_t bit,
                               uint32_t class_match) {
	const auto selected =
	    EmitCompareU32Constant(state, spv::OpINotEqual, EmitAndConstant(state, mask, 1u << bit), 0);
	return EmitLogicalAndBool(state, selected, class_match);
}

static uint32_t EmitClassMaskBits(EmitterState& state, uint32_t bits, uint32_t mask, bool half) {
	const uint32_t sign_mask     = half ? 0x8000u : 0x80000000u;
	const uint32_t exponent_mask = half ? 0x7c00u : 0x7f800000u;
	const uint32_t mantissa_mask = half ? 0x03ffu : 0x007fffffu;
	const uint32_t quiet_mask    = half ? 0x0200u : 0x00400000u;
	const auto     sign_bits     = EmitAndConstant(state, bits, sign_mask);
	const auto     abs_bits      = EmitAndConstant(state, bits, sign_mask - 1u);
	const auto     exponent_bits = EmitAndConstant(state, abs_bits, exponent_mask);
	const auto     mantissa_bits = EmitAndConstant(state, abs_bits, mantissa_mask);
	const auto     quiet_bits    = EmitAndConstant(state, mantissa_bits, quiet_mask);

	const auto sign             = EmitCompareU32Constant(state, spv::OpINotEqual, sign_bits, 0);
	const auto positive         = EmitLogicalNotBool(state, sign);
	const auto exponent_zero    = EmitCompareU32Constant(state, spv::OpIEqual, exponent_bits, 0);
	const auto exponent_nonzero = EmitLogicalNotBool(state, exponent_zero);
	const auto exponent_inf =
	    EmitCompareU32Constant(state, spv::OpIEqual, exponent_bits, exponent_mask);
	const auto finite_exponent  = EmitLogicalNotBool(state, exponent_inf);
	const auto mantissa_zero    = EmitCompareU32Constant(state, spv::OpIEqual, mantissa_bits, 0);
	const auto mantissa_nonzero = EmitLogicalNotBool(state, mantissa_zero);
	const auto quiet            = EmitCompareU32Constant(state, spv::OpINotEqual, quiet_bits, 0);

	const auto nan_common = EmitLogicalAndBool(state, exponent_inf, mantissa_nonzero);
	const auto snan       = EmitLogicalAndBool(state, nan_common, EmitLogicalNotBool(state, quiet));
	const auto qnan       = EmitLogicalAndBool(state, nan_common, quiet);
	const auto inf        = EmitLogicalAndBool(state, exponent_inf, mantissa_zero);
	const auto normal     = EmitLogicalAndBool(state, exponent_nonzero, finite_exponent);
	const auto denorm     = EmitLogicalAndBool(state, exponent_zero, mantissa_nonzero);
	const auto zero       = EmitCompareU32Constant(state, spv::OpIEqual, abs_bits, 0);

	uint32_t match = EmitClassMaskBitMatch(state, mask, 0, snan);
	match          = EmitLogicalOrBool(state, match, EmitClassMaskBitMatch(state, mask, 1, qnan));
	match          = EmitLogicalOrBool(
	    state, match, EmitClassMaskBitMatch(state, mask, 2, EmitLogicalAndBool(state, inf, sign)));
	match = EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 3, EmitLogicalAndBool(state, normal, sign)));
	match = EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 4, EmitLogicalAndBool(state, denorm, sign)));
	match = EmitLogicalOrBool(
	    state, match, EmitClassMaskBitMatch(state, mask, 5, EmitLogicalAndBool(state, zero, sign)));
	match = EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 6, EmitLogicalAndBool(state, zero, positive)));
	match = EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 7, EmitLogicalAndBool(state, denorm, positive)));
	match = EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 8, EmitLogicalAndBool(state, normal, positive)));
	return EmitLogicalOrBool(
	    state, match,
	    EmitClassMaskBitMatch(state, mask, 9, EmitLogicalAndBool(state, inf, positive)));
}

uint32_t EmitClassMaskF32(EmitterState& state, uint32_t value, uint32_t mask) {
	return EmitClassMaskBits(state, EmitBitcastF32ToU32(state, value), mask, false);
}

uint32_t EmitClassMaskF16(EmitterState& state, uint32_t bits, uint32_t mask) {
	return EmitClassMaskBits(state, bits, mask, true);
}

uint32_t EmitMinMaxF32Value(EmitterState& state, uint32_t lhs, uint32_t rhs, bool max_value) {
	const auto lhs_class = EmitClassifyF32(state, lhs);
	const auto rhs_class = EmitClassifyF32(state, rhs);

	const auto numeric_cond = state.builder.AllocateId();
	state.builder.AddFunction(max_value ? spv::OpFOrdGreaterThanEqual : spv::OpFOrdLessThan,
	                          TypeBool(state), numeric_cond, lhs, rhs);
	const auto ordered_bits =
	    EmitSelectValueU32(state, numeric_cond, lhs_class.bits, rhs_class.bits);

	// Both are zeros when neither has a bit set besides the sign.
	const auto both_zero    = EmitCompareU32Constant(
	    state, spv::OpIEqual,
	    EmitAndConstant(state, EmitOrU32(state, lhs_class.bits, rhs_class.bits), 0x7fffffffu), 0);
	const auto zero_bits    = max_value ? EmitAndU32(state, lhs_class.bits, rhs_class.bits)
	                                    : EmitOrU32(state, lhs_class.bits, rhs_class.bits);
	const auto numeric_bits = EmitSelectValueU32(state, both_zero, zero_bits, ordered_bits);

	const auto rhs_nan_bits =
	    EmitSelectValueU32(state, rhs_class.nan, lhs_class.bits, numeric_bits);
	const auto result_bits = EmitSelectValueU32(state, lhs_class.nan, rhs_class.bits, rhs_nan_bits);
	return EmitBitcastU32ToF32(state, result_bits);
}

uint32_t EmitFlushF32DenormToSignedZero(EmitterState& state, uint32_t value) {
	// A zero exponent field means a zero or a denormal. Keeping only the sign bit gives the same
	// zero for a zero and flushes a denormal to the zero of its sign, so no zero test is needed.
	const auto bits          = Unary(state, spv::OpBitcast, TypeU32(state), value);
	const auto exponent_zero = EmitCompareU32Constant(
	    state, spv::OpIEqual, EmitAndConstant(state, bits, 0x7f800000u), 0);
	const auto selected = EmitSelectValueU32(state, exponent_zero,
	                                         EmitAndConstant(state, bits, 0x80000000u), bits);
	return Unary(state, spv::OpBitcast, TypeF32(state), selected);
}

uint32_t EmitTrigCycleF32(EmitterState& state, uint32_t src, bool preserve_signed_zero) {
	// A finite value of magnitude 2^23 or more is a whole number, so its fraction is +0 without
	// a test for it (VectorSinCosLargeFiniteSpecialCases).
	const auto reduced = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpExtInst, TypeF32(state), reduced, GlslStd450(state),
	                          GLSLstd450Fract, src);
	if (!preserve_signed_zero) {
		return reduced;
	}
	const auto bits = Unary(state, spv::OpBitcast, TypeU32(state), src);
	const auto zero = EmitCompareU32Constant(state, spv::OpIEqual,
	                                         EmitAndConstant(state, bits, 0x7fffffffu), 0);
	const auto ret  = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpSelect, TypeF32(state), ret, zero, src, reduced);
	return ret;
}

uint32_t EmitF16BitsToF32(EmitterState& state, uint32_t bits) {
	const auto unpacked = state.builder.AllocateId();
	const auto ret      = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpExtInst, TypeF32Vector(state, 2), unpacked, GlslStd450(state),
	                          GLSLstd450UnpackHalf2x16, bits);
	state.builder.AddFunction(spv::OpCompositeExtract, TypeF32(state), ret, unpacked, 0);
	return ret;
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
