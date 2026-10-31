#!/usr/bin/env python3
"""Release notes from the commit messages since the previous release tag.

  python3 tools/changelog.py v0.2.0                  # notes for tag v0.2.0 -> stdout
  python3 tools/changelog.py v0.2.0 --since v0.1.0   # explicit previous release
  python3 tools/changelog.py HEAD --since HEAD~3     # preview before tagging

Each commit becomes a section: the subject as a heading and the body (already in
Markdown bullets) below it, without the Co-Authored-By lines. The previous release is
the closest vX.Y.Z tag before the given one; for the first release every commit is
listed by subject only, to keep the page short. Then the official mods (mods/*/mod.ini:
name, version, whether they changed), how to install and check the download, and a link
to the full comparison.
"""

import argparse
import configparser
import os
import re
import subprocess
import sys

TRAILER = re.compile(r"^(Co-Authored-By|Signed-off-by):", re.IGNORECASE)


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def previous_tag(ref: str) -> str | None:
    """The closest vX.Y.Z tag reachable from ref's parent."""
    try:
        return git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", f"{ref}^").strip()
    except subprocess.CalledProcessError:
        return None


def commits(rev_range: str) -> list[tuple[str, str, str]]:
    """(hash, subject, body) from oldest to newest, merges left out."""
    out = git("log", "--reverse", "--no-merges", "--format=%h%x1f%s%x1f%b%x1e", rev_range)
    result = []
    for record in out.split("\x1e"):
        if not record.strip():
            continue
        short, subject, body = record.strip("\n").split("\x1f")
        lines = [l for l in body.splitlines() if not TRAILER.match(l)]
        result.append((short, subject.strip(), "\n".join(lines).strip()))
    return result


def official_mods(ref: str, since: str | None) -> list[str]:
    """One line per mods/<name>/mod.ini at ref: name, version and whether it changed."""
    lines = []
    for path in git("ls-tree", "--name-only", f"{ref}:mods").split() if ref_has(ref, "mods") else []:
        if not ref_has(ref, f"mods/{path}/mod.ini"):
            continue
        parser = configparser.RawConfigParser()
        parser.read_string(git("show", f"{ref}:mods/{path}/mod.ini"))
        info = dict(parser["mod"]) if parser.has_section("mod") else {}
        changed = not since or subprocess.run(["git", "diff", "--quiet", since, ref, "--", f"mods/{path}"]).returncode
        lines.append(f"- **{info.get('name') or path}**, versão {info.get('version', '?')} (`mod-{path}-{info.get('version', '?')}.zip`)"
                     + (" — atualizado nesta versão" if changed else ""))
    return lines


def ref_has(ref: str, path: str) -> bool:
    return subprocess.run(["git", "cat-file", "-e", f"{ref}:{path}"], capture_output=True).returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("tag", help="the release tag (or any ref, for a preview)")
    ap.add_argument("--since", help="previous release (default: the closest earlier vX.Y.Z tag)")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""), help="owner/name, for the compare link")
    args = ap.parse_args()

    since = args.since or previous_tag(args.tag)
    rev_range = f"{since}..{args.tag}" if since else args.tag
    entries = commits(rev_range)
    if not entries:
        sys.exit(f"error: no commits in {rev_range}")

    out = []
    if since:
        out.append(f"## Novidades desde {since}\n")
        for short, subject, body in entries:
            out.append(f"### {subject}\n")
            if body:
                out.append(body + "\n")
            out.append(f"<sub>{short}</sub>\n")
    else:
        out.append("## Primeira release\n")
        out.append("Tudo o que entrou até aqui (o histórico completo está em `docs/development/DIARY.pt-BR.md`):\n")
        out.extend(f"- {subject} ({short})" for short, subject, _ in entries)
        out.append("")

    if mods := official_mods(args.tag, since):
        out.append("## Mods oficiais\n")
        out.extend(mods)
        out.append("")

    out.append("## Como instalar\n")
    out.append("1. Baixe o `ie3ogre-recomp-<versão>-windows.zip` abaixo e descompacte numa pasta em que dê para gravar.\n"
               "2. Abra o `ie3-launcher.exe` e escolha o dump do **seu** cartucho (`.nds`, BOEJ, sem modificações).\n"
               "3. O launcher prepara e compila o jogo no seu PC; depois é só clicar em **Jogar**.\n\n"
               "Quem já tem o launcher pode atualizar em **Atualizações**, no menu do launcher: saves e configurações são mantidos, e o "
               "dump não é pedido de novo.\n\n"
               "O pacote não contém dados nem código do jogo. Para conferir o download, compare o SHA-256 do zip com o "
               "`sha256sums.txt` (no PowerShell: `Get-FileHash ie3ogre-recomp-*.zip`).\n\n"
               "Os outros `.zip` da release são os mods oficiais (lista acima): "
               "coloque-os, sem descompactar, na pasta `mods` ao lado do jogo e ligue-os em **Mods**, no launcher. Eles só "
               "trazem patches e também não contêm dados do jogo.\n")
    if since and args.repo:
        out.append(f"**Todas as mudanças:** https://github.com/{args.repo}/compare/{since}...{args.tag}\n")
    print("\n".join(out).rstrip() + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
