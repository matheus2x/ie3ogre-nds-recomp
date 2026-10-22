// Builds the BOEJ memory map on top of an imported ARM9 static image and applies dsd symbols.
// Run as a headless preScript by "tools/analyze.py ghidra", which writes the manifest.
//
// Argument: <manifest> with one entry per line (tab-separated):
//   main     <symbols.txt>                                 symbols for the imported "ram" block
//   block    <name> <base hex> <data file> <symbols.txt|->  plain block (ITCM, DTCM)
//   overlay  <name> <base hex> <data file> <symbols.txt|->  overlay block with its own address space
//
// All blocks are created and every function's ARM/Thumb mode is set before anything is
// disassembled, so flow from one function never fixes the mode of another.
//@category BOEJ

import java.io.File;
import java.io.FileInputStream;
import java.math.BigInteger;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class ImportBoej extends GhidraScript {

    private static final Pattern SYMBOL = Pattern.compile(
        "^(\\S+) kind:(function|data|bss|label)(?:\\(([^)]*)\\))? addr:(0x[0-9a-fA-F]+)");
    private static final Pattern SIZE = Pattern.compile("size=(0x[0-9a-fA-F]+)");

    private record Sym(String name, String kind, String params, Address addr) {
        boolean thumb() {
            return params.startsWith("thumb");
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ImportBoej <manifest>");
        }
        MemoryBlock ram = currentProgram.getMemory().getBlock(toAddr(0x02000000L));
        ram.setName("ram");
        ram.setWrite(true);
        ram.setExecute(true);

        List<Sym> syms = new ArrayList<>();
        for (String line : Files.readAllLines(new File(args[0]).toPath())) {
            if (line.isBlank()) {
                continue;
            }
            String[] f = line.split("\t");
            switch (f[0]) {
                case "main" -> readSymbols(new File(f[1]), null, syms);
                case "block", "overlay" -> {
                    MemoryBlock block = addBlock(f[1], Long.decode(f[2]), new File(f[3]), f[0].equals("overlay"));
                    if (!f[4].equals("-")) {
                        readSymbols(new File(f[4]), block.getStart().getAddressSpace(), syms);
                    }
                }
                default -> throw new IllegalArgumentException("bad manifest line: " + line);
            }
        }

        Register tmode = currentProgram.getRegister("TMode");
        for (Sym s : syms) {
            if (s.kind.equals("function")) {
                currentProgram.getProgramContext().setValue(tmode, s.addr, s.addr,
                    s.thumb() ? BigInteger.ONE : BigInteger.ZERO);
            }
        }

        int functions = 0, labels = 0, failed = 0;
        for (Sym s : syms) {
            if (!s.kind.equals("function")) {
                createLabel(s.addr, s.name, true, SourceType.IMPORTED);
                labels++;
                continue;
            }
            if (getInstructionAt(s.addr) == null) {
                new ArmDisassembleCommand(s.addr, null, s.thumb()).applyTo(currentProgram, monitor);
            }
            Function func = getFunctionAt(s.addr);
            if (func == null) {
                Matcher sm = SIZE.matcher(s.params);
                try {
                    func = sm.find()
                        ? currentProgram.getFunctionManager().createFunction(s.name, s.addr,
                            new AddressSet(s.addr, s.addr.add(Long.decode(sm.group(1)) - 1)), SourceType.IMPORTED)
                        : createFunction(s.addr, s.name);
                } catch (Exception e) {
                    printerr("could not create function " + s.name + ": " + e.getMessage());
                }
            }
            if (func == null) {
                failed++;
                continue;
            }
            func.setName(s.name, SourceType.IMPORTED);
            functions++;
        }
        println(String.format("ImportBoej: %d functions, %d labels, %d failed", functions, labels, failed));
    }

    private MemoryBlock addBlock(String name, long base, File data, boolean overlay) throws Exception {
        try (FileInputStream in = new FileInputStream(data)) {
            MemoryBlock block = currentProgram.getMemory().createInitializedBlock(
                name, toAddr(base), in, data.length(), monitor, overlay);
            block.setRead(true);
            block.setWrite(true);
            block.setExecute(true);
            return block;
        }
    }

    private void readSymbols(File file, AddressSpace space, List<Sym> out) throws Exception {
        for (String line : Files.readAllLines(file.toPath())) {
            Matcher m = SYMBOL.matcher(line);
            if (!m.find()) {
                continue;
            }
            long offset = Long.decode(m.group(4));
            Address addr = space == null ? toAddr(offset) : space.getAddress(offset);
            out.add(new Sym(m.group(1), m.group(2), m.group(3) == null ? "" : m.group(3), addr));
        }
    }
}
