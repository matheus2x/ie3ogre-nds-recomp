// Decompiles functions from the BOEJ project and writes them to a text file.
// Run with "tools/analyze.py decompile <addr|name>...". Arguments:
//   <output file> <target>...   target = 0xADDRESS, ovNNN:0xADDRESS or a function name
//@category BOEJ

import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class Decompile extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Function f = resolve(args[i]);
                if (f == null) {
                    out.printf("// %s: no function%n%n", args[i]);
                    continue;
                }
                DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                out.printf("// %s @ %s (%d bytes)%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
                out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// " + res.getErrorMessage());
                out.println();
            }
        }
        ifc.dispose();
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
