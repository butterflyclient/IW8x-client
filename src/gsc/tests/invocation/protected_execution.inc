// Included once inside the existing instrumented test fixture.
uint32_t execute(void* ctx, uint32_t thread, const uint8_t* code, uint32_t count)
{
	expect(count == 0 && code[0] == 0x4b && code[1] == 0x1d && code[4] == 0x2f, "protected invocation program");
	auto* floor = top(ctx);
	field<uint32_t>(ctx, core::replay::layout::frame_count) = 1;
	raw* local_top;
	if (setjmp(script_jump) == 0)
	{
		core::invocation_trampoline(ctx);
		clear(ctx);
		if (!inputs(ctx)) add_undefined(ctx);
		local_top = top(ctx);
	}
	else
	{
		// Retail 36422c7 cleans outparams only; 3642332 appends undefined in
		// the interpreter's local stack. Return (363ee3b) frees residual
		// inparams down to the frame marker, including partial marshalling.
		clear(ctx);
		local_top = top(ctx) + 1;
		*local_top = {};
	}
	const auto result = *local_top--;
	while (local_top > floor)
	{
		remove_ref(ctx, local_top->type, local_top->payload);
		*local_top-- = {};
	}
	top(ctx) = floor + 1;
	*top(ctx) = result;
	inputs(ctx) = 1;
	outputs(ctx) = 0;
	field<uint32_t>(ctx, core::replay::layout::frame_count) = 0;
	return thread;
}
