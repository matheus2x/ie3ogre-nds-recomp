// Prints the disassembly of functions from the BOEJ project.
// Run with "tools/analyze.py asm <target>...". Arguments:
//   <output file> <target>...   target = 0xADDRESS, ovNNN:0xADDRESS or a function name
//@category BOEJ

import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class Disasm extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Function f = resolve(args[i]);
                if (f == null) {
                    out.printf("; %s: no function%n%n", args[i]);
                    continue;
                }
                out.printf("; %s @ %s%n", f.getName(), f.getEntryPoint());
                for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    out.printf("  %s  %s%n", insn.getAddress(), insn);
                }
                out.println();
            }
        }
    }

    private Function resolve(String target) {
        if (!target.contains("0x")) {
            var funcs = getGlobalFunctions(target);
            return funcs.isEmpty() ? null : funcs.get(0);
        }
        String[] parts = target.split(":");
        Address addr = parts.length == 2
            ? currentProgram.getAddressFactory().getAddressSpace(parts[0]).getAddress(Long.decode(parts[1]))
            : toAddr(Long.decode(parts[0]));
        Function f = getFunctionAt(addr);
        return f != null ? f : getFunctionContaining(addr);
    }
}
