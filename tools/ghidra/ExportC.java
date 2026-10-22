// Decompiles every function in the given memory blocks into one C-like file per block.
// Run with "tools/analyze.py export <block>...". Arguments: <output dir> <block>...
//@category BOEJ

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class ExportC extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(args[0]);
        dir.mkdirs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            MemoryBlock block = currentProgram.getMemory().getBlock(args[i]);
            if (block == null) {
                printerr("no block " + args[i]);
                continue;
            }
            try (PrintWriter out = new PrintWriter(new File(dir, args[i] + ".c"), "UTF-8")) {
                for (Function f : currentProgram.getFunctionManager().getFunctions(
                        new ghidra.program.model.address.AddressSet(block.getStart(), block.getEnd()), true)) {
                    DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                    out.printf("// @ %s size=%#x%n", f.getEntryPoint(), f.getBody().getNumAddresses());
                    out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC()
                        : "// " + f.getName() + ": " + res.getErrorMessage());
                }
            }
        }
        ifc.dispose();
    }
}
