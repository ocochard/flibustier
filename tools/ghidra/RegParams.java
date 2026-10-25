/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Ghidra headless pre-decompile script: name the game's small helpers
 * that take their arguments in registers (AX, BX) and give them those
 * signatures, so the decompiler shows the operands at every call site.
 * Without it, calls appear as FUN_00009c5e() with no arguments.
 *
 * Add a helper by adding a line to HELPERS: address, name, return
 * register, then the parameter registers in order.
 */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.ShortDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.symbol.SourceType;

public class RegParams extends GhidraScript {
	private static final String[][] HELPERS = {
		/* address, name, return, parameters... */
		{ "0x9c42", "rand_word", "AX" },
		{ "0x9c51", "abs16", "AX", "AX" },
		{ "0x9c5e", "div16", "AX", "AX", "BX" },
		{ "0x23150", "sine_mul", "AX", "AX", "BX" },
		{ "0x23146", "cosine_mul", "AX", "AX", "BX" },
	};

	@Override
	public void run() throws Exception {
		DataType word = ShortDataType.dataType;

		for (String[] h : HELPERS) {
			Function f = getFunctionAt(toAddr(Long.decode(h[0])));
			if (f == null)
				f = createFunction(toAddr(Long.decode(h[0])), h[1]);
			if (f == null) {
				printerr("no function at " + h[0]);
				continue;
			}
			f.setName(h[1], SourceType.USER_DEFINED);
			f.setCustomVariableStorage(true);
			Variable[] params = new Variable[h.length - 3];
			for (int i = 3; i < h.length; i++) {
				Register r = currentProgram.getRegister(h[i]);
				params[i - 3] = new ParameterImpl(
				    "r" + h[i].toLowerCase(), word, r,
				    currentProgram);
			}
			f.replaceParameters(FunctionUpdateType.CUSTOM_STORAGE,
			    true, SourceType.USER_DEFINED, params);
			f.setReturn(word, new VariableStorage(currentProgram,
			    currentProgram.getRegister(h[2])),
			    SourceType.USER_DEFINED);
			println("helper " + h[1] + " at " + h[0]);
		}
	}
}
