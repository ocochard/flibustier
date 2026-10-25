/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Ghidra headless post-script: decompile every function into
 * <out>/decomp.c and list instructions whose scalar operands fall in
 * [lo, hi) into <out>/refs.txt.  Arguments: <out> <lo> <hi>.
 */
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;

public class DumpAll extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		String out = a[0];
		long lo = Long.decode(a[1]), hi = Long.decode(a[2]);
		DecompInterface d = new DecompInterface();
		d.openProgram(currentProgram);
		try (PrintWriter w = new PrintWriter(new FileWriter(out + "/decomp.c"))) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				DecompileResults r = d.decompileFunction(f, 60, monitor);
				w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
			}
		}
		try (PrintWriter w = new PrintWriter(new FileWriter(out + "/refs.txt"))) {
			InstructionIterator it = currentProgram.getListing().getInstructions(true);
			while (it.hasNext()) {
				Instruction ins = it.next();
				for (int i = 0; i < ins.getNumOperands(); i++)
					for (Object o : ins.getOpObjects(i))
						if (o instanceof Scalar) {
							long v = ((Scalar) o).getUnsignedValue();
							if (v >= lo && v < hi) {
								Function f = getFunctionContaining(ins.getAddress());
								w.println(ins.getAddress() + " " + String.format("0x%x", v) + " " + (f == null ? "-" : f.getName()) + " " + ins);
							}
						}
			}
		}
		println("functions: " + currentProgram.getFunctionManager().getFunctionCount());
	}
}
