# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause

"""The licences of what a demo package carries beside plotcore.

plotcore is BSD-3-Clause, and nothing else in a package is plotcore's: each
demo carries a toolkit, and each toolkit carries code of its own -- the egui
demo some hundred Rust crates compiled into one executable, the Qt demo Qt and
what Qt builds in, the PyQt6 demo a Python, a dozen distributions and a Qt of
its own, and both Qt images the libraries they take from AlmaLinux. Every one
of those asks for its licence and its copyright lines to go with the binary,
and the copyleft ones for their source to be offered too. A package that went
out with plotcore's LICENSE alone, as every one did until 0.2.1, kept none of
those promises.

Each subcommand writes one section of a package's licences folder, each from
what describes that kind of code best, and each fails rather than writes a
section with a hole in it:

    rust     the crates `cargo tree` says the executable links, with the files
             each crate ships, and the standard library's own notices
    python   every distribution installed beside the interpreter, with the
             files its dist-info declares, and the interpreter's own
    qt       Qt's third-party code, from the SBOM Qt installs beside itself,
             cut to what the shipped libraries and plugins contain
    system   the libraries an AppImage takes from the build machine, with the
             licence files of the package that installed each

and `check` reads a finished package back, for the verifiers: every Qt binary
in it is one the Qt section names, and every system library one the system
section does.

The Python standard library is all this uses, so that it runs under the
interpreter a package carries -- which is how `python` sees that
interpreter's distributions and nothing of the machine's.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

LICENCES = Path(__file__).resolve().parent / "licenses"

# What is kept under licenses/, beside the README every package carries and
# the GPL and LGPL texts every package with Qt in it does, is what a section
# cannot be written without and its sources do not carry:
#
#   texts/       licence texts by SPDX identifier, for those Qt's SBOMs name,
#                from SPDX's license-list-data at v3.29.0
#   icu/73/      ICU 73's LICENSE, from unicode-org/icu at release-73-2
#   crates/      by repository, the licence files of the projects whose crates
#                ship none: egui at its tag 0.37.0, and accesskit, profiling
#                and clipboard-win at the commits their crates were published
#                from (.cargo_vcs_info.json)
#   pyqt6-qt6/   the Qt section for PyQt6-Qt6, by version and platform,
#                written `qt --lean` from Qt's own SBOMs for that version and
#                the binaries each package keeps of it

# A file whose name says it is a licence, a copyright statement or a notice.
LICENCE_NAME = re.compile(r"(?i)(licen[cs]e|copying|copyright|notice)")


def fail(message: str) -> None:
    print(f"notices: {message}", file=sys.stderr)
    sys.exit(1)


def run(*command: str, cwd: Optional[Path] = None) -> str:
    return subprocess.run(
        command, cwd=cwd, check=True, stdout=subprocess.PIPE, text=True
    ).stdout


def copy_into(directory: Path, files: Iterable[Tuple[Path, str]]) -> List[str]:
    """Copies each (source, relative name) under directory; the names, sorted."""
    names = []
    for source, name in files:
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        names.append(name)
    return sorted(names)


def links(section: str, names: List[str]) -> str:
    return ", ".join(f"[{Path(n).name}]({section}/{n})" for n in names)


def write_index(path: Path, title: str, preamble: str, header: List[str], rows: List[List[str]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [f"# {title}", "", preamble.strip(), ""]
    lines.append("| " + " | ".join(header) + " |")
    lines.append("|" + "---|" * len(header))
    for row in rows:
        lines.append("| " + " | ".join(cell.replace("|", "\\|") for cell in row) + " |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# --- rust ------------------------------------------------------------------
# What cargo builds the executable from, for the one target it is built for:
# normal edges only, so that a build script's dependencies and a test's are
# not counted, and no proc-macro, which runs in the compiler and leaves
# nothing in the binary. A crate that ships no licence file -- egui's crates
# among them -- takes its project's, kept under licenses/crates/ by
# repository and taken from the commit or tag the crate was published at.


def crate_licence_files(root: Path) -> List[Tuple[Path, str]]:
    files = [(p, p.name) for p in root.iterdir() if p.is_file() and LICENCE_NAME.search(p.name)]
    # A font's licence sits beside the font, under whatever name its foundry
    # gave it (epaint_default_fonts: OFL.txt, UFL.txt, Hack-Regular.txt).
    for font_dir in {p.parent for p in root.rglob("*") if p.suffix.lower() in (".ttf", ".otf")}:
        for text in font_dir.glob("*.txt"):
            files.append((text, text.relative_to(root).as_posix()))
    return files


def vendored_crate_licences(repository: Optional[str]) -> List[Tuple[Path, str]]:
    match = re.match(r"https?://([^/]+)/([^/]+)/([^/#?]+)", repository or "")
    if not match:
        return []
    directory = LICENCES / "crates" / match[1] / match[2] / match[3].removesuffix(".git")
    if not directory.is_dir():
        return []
    return [(p, p.name) for p in sorted(directory.iterdir()) if p.is_file()]


def rust(out: Path, workspace: Path, package: str, target: str) -> None:
    section = out / "rust"
    tree = run(
        "cargo", "tree", "--locked", "-p", package, "--target", target,
        "-e", "normal,no-proc-macro", "--prefix", "none", "--format", "{p}",
        cwd=workspace,
    )
    linked = set()
    for line in tree.splitlines():
        words = line.replace(" (*)", "").split()
        # A workspace crate prints its path; it is plotcore, not a third party.
        if len(words) == 2:
            linked.add((words[0], words[1].lstrip("v")))
    metadata = json.loads(run(
        "cargo", "metadata", "--locked", "--format-version", "1", "--filter-platform", target,
        cwd=workspace,
    ))
    packages = {(p["name"], p["version"]): p for p in metadata["packages"]}

    rows = []
    for name, version in sorted(linked):
        crate = packages[(name, version)]
        root = Path(crate["manifest_path"]).parent
        files = crate_licence_files(root)
        if not any("/" not in n for _, n in files):
            files += vendored_crate_licences(crate.get("repository"))
        if not files:
            fail(f"crate {name} {version} ships no licence file, and there is none for "
                 f"{crate.get('repository')} under {LICENCES / 'crates'}")
        directory = f"{name}-{version}"
        names = copy_into(section / directory, files)
        rows.append([name, version, crate.get("license") or "", links(directory, names)])

    # The standard library is compiled in too, and the toolchain says how.
    sysroot = Path(run("rustc", "--print", "sysroot", cwd=workspace).strip())
    docs = sysroot / "share" / "doc" / "rust"
    std = [docs / "COPYRIGHT-library.html"] + sorted((docs / "licenses").glob("*"))
    if not std[0].is_file():
        fail(f"the toolchain at {sysroot} has no {std[0].relative_to(sysroot)}")
    version = run("rustc", "--version", cwd=workspace).split()[1]
    names = copy_into(section / "std", [(p, p.relative_to(docs).as_posix()) for p in std])
    rows.insert(0, ["Rust standard library", version, "MIT OR Apache-2.0", links("std", names)])

    write_index(
        section / "index.md",
        "Rust crates",
        f"""
The executable is compiled from these crates for {target}, beside plotcore
and the Rust standard library. Each directory holds the licence files the
crate ships -- or, where it ships none, those of the project it is published
from. Where a crate offers a choice of licences, it is taken under the first.
""",
        ["Crate", "Version", "Licence", "Files"],
        rows,
    )


# --- python ----------------------------------------------------------------
# Run under the interpreter the package carries, so that what it lists is
# what that interpreter imports. Every distribution installed beside it is
# listed, which on Windows includes PyInstaller and what it needs to build:
# PyInstaller's runtime hooks are frozen into the program, and the rest is
# a notice too many rather than one too few.


def distribution_licence_files(dist) -> List[Tuple[Path, str]]:
    files = []
    for entry in dist.files or []:
        parts = Path(entry).parts
        if len(parts) < 2 or not parts[0].endswith(".dist-info"):
            continue
        # PEP 639 puts every licence file under licenses/; before it, they
        # sat in the dist-info itself under their own names.
        if parts[1] == "licenses" or LICENCE_NAME.search(parts[-1]):
            path = Path(dist.locate_file(entry))
            if path.is_file():
                files.append((path, Path(*parts[1:]).as_posix()))
    return files


def distribution_licence(metadata) -> str:
    expression = metadata.get("License-Expression")
    if expression:
        return expression
    classifiers = [c.split(" :: ")[-1] for c in metadata.get_all("Classifier") or []
                   if c.startswith("License :: ")]
    if classifiers:
        return " / ".join(classifiers)
    text = (metadata.get("License") or "").strip().splitlines()
    return text[0][:60] if text else ""


# Where a distribution's source is, when it is not its sdist on PyPI.
SOURCES = {
    "plotcore": "[this release](https://github.com/jonassattler/plotcore/releases)",
    "PyQt6-Qt6": "[Qt's](../qt/index.md)",
}


def python(out: Path, interpreter_licences: Optional[Path]) -> None:
    import importlib.metadata
    import sysconfig

    section = out / "python"
    rows = []

    version = ".".join(map(str, sys.version_info[:3]))
    # The LICENSE.txt python.org's Windows build installs covers what it
    # links in as well; a Unix install has only CPython's, beside the
    # standard library, and python-build-standalone publishes the rest --
    # CPython's among them -- which is what --interpreter-licences is for.
    if interpreter_licences:
        files = [(p, p.name) for p in sorted(interpreter_licences.iterdir()) if p.is_file()]
    else:
        interpreter = [Path(sys.base_prefix) / "LICENSE.txt",
                       Path(sysconfig.get_paths()["stdlib"]) / "LICENSE.txt"]
        files = [(p, p.name) for p in interpreter if p.is_file()][:1]
    if not files:
        fail(f"Python {version} at {sys.base_prefix} has no LICENSE.txt")
    names = copy_into(section / f"python-{version}", files)
    rows.append(["Python", version, "PSF-2.0", links(f"python-{version}", names),
                 f"[python.org](https://www.python.org/downloads/release/python-{version.replace('.', '')}/)"])

    seen = set()
    for dist in sorted(importlib.metadata.distributions(),
                       key=lambda d: d.metadata["Name"].lower()):
        name = dist.metadata["Name"]
        if name.lower() in seen:
            continue
        seen.add(name.lower())
        files = distribution_licence_files(dist)
        if not files:
            fail(f"{name} {dist.version} carries no licence file in its dist-info")
        directory = f"{name}-{dist.version}"
        names = copy_into(section / directory, files)
        source = SOURCES.get(name, f"[PyPI](https://pypi.org/project/{name}/{dist.version}/#files)")
        rows.append([name, dist.version, distribution_licence(dist.metadata),
                     links(directory, names), source])

    write_index(
        section / "index.md",
        "Python",
        f"""
The Python this package runs on, and every distribution installed beside it,
with the licence files each declares. A wheel that bundles libraries of its
own -- numpy and Pillow do -- names them, and their licences, in those files.
PyQt6-Qt6 is a build of Qt, of which this package carries only part, as the
Qt section says.
""",
        ["Distribution", "Version", "Licence", "Files", "Source"],
        rows,
    )


# --- qt --------------------------------------------------------------------
# Qt 6.8 and newer install an SPDX document per repository beside
# themselves (sbom/qtbase-6.8.3.spdx.json): every module, plugin and tool,
# what each was built from, and every piece of third-party code with its
# licence and its copyright lines, as Qt's own attribution files record
# them. A package carries some of Qt's libraries and plugins, and what it
# owes notices for is what those contain, so the document is read from the
# files: each shipped binary is found by name among the files a package
# CONTAINS, and the third-party packages that package DEPENDS_ON -- and
# theirs -- are what is listed. Starting from the modules rather than the
# files would bring in what the tools and the tests are built from: a
# package does not carry Catch2, or the kernel headers QtTest includes.
#
# ICU, which Qt's Linux binaries carry a build of, is in no document, and
# its licence is kept under licenses/icu/ by major version.
#
# PyQt6-Qt6 carries no SBOM, so what this writes for it is written once from
# Qt's own for the same version and kept under licenses/pyqt6-qt6/ -- lean,
# naming the texts kept under licenses/ rather than holding copies of them.
# Given such a directory instead of an SBOM, this checks that it covers what
# the package carries, and copies it with those texts.

THIRD_PARTY = ("-qt-3rdparty-sources-", "-qt-bundled-3rdparty-module-")


def licence_ids(expression: str) -> Set[str]:
    return {t for t in re.findall(r"[A-Za-z0-9.+-]+", expression)
            if t not in ("AND", "OR", "WITH", "NOASSERTION", "NONE")}


def shipped_name(name: str) -> str:
    """libQt6Core.so.6.8.3 and libQt6Core.so.6 both as libQt6Core.so."""
    return re.sub(r"(\.so)(\.\d+)*$", r"\1", name)


def icu_major(name: str) -> Optional[str]:
    match = re.match(r"(?:lib)?icu\w*?\.so\.(\d+)$|icu\w*?(\d+)\.dll$", name)
    return (match[1] or match[2]) if match else None


def render_qt(out: Path, sboms: List[Path], binaries: List[str], lean: bool) -> None:
    """Writes the section; lean leaves out the texts kept under LICENCES."""
    section = out / "qt"
    wanted = {shipped_name(b) for b in binaries if not icu_major(b)}
    covered: Set[str] = set()
    components: Dict[Tuple[str, str, str, str], None] = {}
    texts: Dict[str, Optional[str]] = {}
    versions = set()

    for path in sboms:
        document = json.loads(path.read_text(encoding="utf-8"))
        versions.add(document["name"].rsplit("-", 1)[-1])
        packages = {p["SPDXID"]: p for p in document["packages"]}
        files = {f["SPDXID"]: Path(f["fileName"]).name for f in document.get("files", [])}
        for info in document.get("hasExtractedLicensingInfos", []):
            texts.setdefault(info["licenseId"], info.get("extractedText"))
        contains: Dict[str, List[str]] = {}
        depends: Dict[str, List[str]] = {}
        for r in document["relationships"]:
            a, kind, b = r["spdxElementId"], r["relationshipType"], r["relatedSpdxElement"]
            if kind == "CONTAINS" and b in files:
                contains.setdefault(a, []).append(shipped_name(files[b]))
            elif kind == "DEPENDS_ON" and b in packages:
                depends.setdefault(a, []).append(b)

        todo = []
        for package, names in contains.items():
            hits = wanted.intersection(names)
            if hits and package in packages:
                covered |= hits
                todo += [d for d in depends.get(package, []) if any(t in d for t in THIRD_PARTY)]
        seen = set()
        while todo:
            ref = todo.pop()
            if ref in seen:
                continue
            seen.add(ref)
            todo += depends.get(ref, [])
            p = packages[ref]
            if not any(t in ref for t in THIRD_PARTY):
                continue
            licence = p.get("licenseConcluded") or p.get("licenseDeclared") or "NOASSERTION"
            copyright_ = p.get("copyrightText") or "NOASSERTION"
            # Core_Attribution_doubleconversion, BundledLibpng: the component
            # is the last word, whichever target names it.
            name = re.sub(r"^Bundled", "", re.sub(r"^\w+?_Attribution_", "", p["name"]))
            version = p.get("versionInfo", "")
            components[(name, "" if version == "unknown" else version, licence, copyright_)] = None

    missing = sorted(wanted - covered)
    if missing:
        fail(f"no SBOM in {sorted({str(s.parent) for s in sboms})} names {', '.join(missing)}")
    if len(versions) != 1:
        fail(f"the SBOMs are of more than one Qt: {sorted(versions)}")
    version = versions.pop()

    needed = set()
    for _, _, licence, _ in components:
        needed |= licence_ids(licence)
    (section / "texts").mkdir(parents=True, exist_ok=True)
    for licence in sorted(needed):
        kept = LICENCES / "texts" / f"{licence}.txt"
        target = section / "texts" / f"{licence}.txt"
        if kept.is_file():
            if not lean:
                shutil.copyfile(kept, target)
        elif texts.get(licence):
            target.write_text(texts[licence].strip() + "\n", encoding="utf-8")
        else:
            fail(f"no text for {licence}: add it to {LICENCES / 'texts'}")

    lines = [
        f"# Qt {version}",
        "",
        "The Qt libraries and plugins this package carries are Qt's, and Qt is",
        "under the LGPL-3.0 ([LGPL-3.0.txt](../LGPL-3.0.txt), which is the",
        "[GPL-3.0](../GPL-3.0.txt) with the permissions it adds). Their source is",
        f"<https://download.qt.io/archive/qt/{version.rsplit('.', 1)[0]}/{version}/submodules/>.",
        "",
        "Qt builds the third-party code below into them. Each is named with its",
        "licence and copyright as Qt's own SBOM records them, cut to what the",
        "binaries listed at the end contain; the licences are in [texts](texts/).",
        "",
    ]
    for name, component_version, licence, copyright_ in sorted(components, key=lambda c: c[0].lower()):
        ids = sorted(licence_ids(licence))
        lines.append(f"## {name}" + (f" {component_version}" if component_version else ""))
        lines.append("")
        lines.append("Licence: " + licence + " (" + ", ".join(
            f"[{i}](texts/{i}.txt)" for i in ids) + ")")
        lines.append("")
        if copyright_ != "NOASSERTION":
            lines += ["    " + c for c in copyright_.strip().splitlines()]
            lines.append("")

    majors = sorted({icu_major(b) for b in binaries if icu_major(b)})
    for major in majors:
        kept = LICENCES / "icu" / major / "LICENSE"
        if not kept.is_file():
            fail(f"there is no licence for ICU {major} at {kept}")
        if not lean:
            shutil.copyfile(kept, section / f"ICU-{major}-LICENSE.txt")
        lines += [f"## ICU {major}", "",
                  "Qt's own build of ICU, beside its libraries; its licence, and those of",
                  f"the data it carries, are in [ICU-{major}-LICENSE.txt](ICU-{major}-LICENSE.txt).",
                  ""]

    shipped = sorted(set(binaries))
    lines += ["## What this covers", ""] + [f"- {b}" for b in shipped]
    (section / "index.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (section / "covers.txt").write_text("\n".join(shipped) + "\n", encoding="utf-8")


def qt(out: Path, source: Path, binaries: List[str], lean: bool) -> None:
    binaries = sorted({Path(b).name for b in binaries})
    sboms = sorted(source.glob("*.spdx.json"))
    if sboms:
        render_qt(out, sboms, binaries, lean)
        return
    covers = source / "covers.txt"
    if not covers.is_file():
        fail(f"{source} is neither Qt's sbom directory nor one this wrote")
    extra = sorted(set(binaries) - set(covers.read_text(encoding="utf-8").split()))
    if extra:
        fail(f"{source} was written for less than this package carries ({', '.join(extra)}); "
             f"write it again with `notices.py qt --lean` from Qt's SBOMs for that version")
    section = out / "qt"
    shutil.copytree(source, section, dirs_exist_ok=True)
    # A lean rendering names the texts kept here rather than carrying them.
    index = (section / "index.md").read_text(encoding="utf-8")
    for name in sorted(set(re.findall(r"\(texts/([^)]+)\.txt\)", index))):
        if not (section / "texts" / f"{name}.txt").is_file():
            shutil.copyfile(LICENCES / "texts" / f"{name}.txt", section / "texts" / f"{name}.txt")
    for major in sorted(set(re.findall(r"\(ICU-(\d+)-LICENSE\.txt\)", index))):
        shutil.copyfile(LICENCES / "icu" / major / "LICENSE", section / f"ICU-{major}-LICENSE.txt")


# --- system ----------------------------------------------------------------
# An AppImage carries libraries from the machine it was built on -- inside
# manylinux_2_28, AlmaLinux 8's and EPEL 8's. Each is traced to the package
# that installed it, and the package's licence files go with it. That image
# installs with tsflags=nodocs, which leaves out what a package marks as
# documentation, so a package that marks its licence that way has it taken
# from the package itself, downloaded for the purpose.
#
# Their source is the other half of what the LGPL and the GPL ask, and
# --record collects the exact package each library came from, so that
# in-container.sh can download their source RPMs and the release publish
# them beside the images. On a Debian or an Ubuntu, for a build of one's
# own, a library's notice is its package's copyright file.

SYSTEM_DIRS = [Path(d) for d in ("/usr/lib64", "/lib64", "/usr/lib/x86_64-linux-gnu",
                                 "/lib/x86_64-linux-gnu", "/usr/lib", "/lib")]


def system_file(name: str) -> List[Path]:
    """Where the machine has the library: the path found, then what it resolves to."""
    for directory in SYSTEM_DIRS:
        if (directory / name).exists():
            return [directory / name, (directory / name).resolve()]
    fail(f"{name} is not a library of this machine's, so nothing says whose it is")
    return []


def dpkg_owner(paths: List[Path]) -> str:
    # dpkg records the path a package installed, which on a merged /usr may
    # be the one before or the one after the link.
    for path in paths:
        found = subprocess.run(["dpkg-query", "-S", str(path)], stdout=subprocess.PIPE, text=True)
        if found.returncode == 0:
            return found.stdout.split(":")[0]
    fail(f"no package owns {paths[0]}")
    return ""


def unpack_rpm(nevra: str, source: bool, members: List[str]) -> Path:
    """Downloads a package, or its source package, and unpacks the members named."""
    unpacked = Path(tempfile.mkdtemp(prefix="notices-rpm-"))
    run("dnf", "download", "-q", *(["--source"] if source else []), "--destdir", str(unpacked), nevra)
    rpm = next(unpacked.glob("*.rpm"))
    cpio = subprocess.run(["rpm2cpio", str(rpm)], check=True, stdout=subprocess.PIPE).stdout
    subprocess.run(["cpio", "-idmu", "--quiet"] + members, input=cpio, cwd=unpacked, check=True)
    return unpacked


def rpm_licence_files(package: str, nevra: str) -> List[Path]:
    listed = run("rpm", "-qL", package).splitlines()
    listed += [p for p in run("rpm", "-qd", package).splitlines() if LICENCE_NAME.search(Path(p).name)]
    listed = [p for p in listed if p.startswith("/")]
    present = [Path(p) for p in listed if Path(p).is_file()]
    if present:
        return present
    if listed:
        # Listed, and left out by nodocs: take them from the package itself.
        unpacked = unpack_rpm(nevra, False, ["." + p for p in listed])
        got = [unpacked / p.lstrip("/") for p in listed if (unpacked / p.lstrip("/")).is_file()]
        if got:
            return got
    # Some packages install no licence at all (AlmaLinux 8's libxcb); the
    # upstream release their source package is built from has one, at the top
    # of its tarball -- or, in a few (xcb-util-keysyms), only in the header of
    # each source file, which is then where it is taken from.
    import tarfile

    unpacked = unpack_rpm(nevra, True, ["*.tar.*", "*.tgz"])
    archives = sorted(p for p in unpacked.iterdir() if ".tar" in p.name or p.suffix == ".tgz")
    found = []
    headers: Dict[str, List[str]] = {}
    for archive in archives:
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if not member.isfile():
                    continue
                if len(parts) <= 2 and LICENCE_NAME.search(parts[-1]):
                    target = unpacked / "licences" / archive.name / parts[-1]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(tar.extractfile(member).read())
                    found.append(target)
                elif member.name.endswith((".c", ".h")):
                    text = tar.extractfile(member).read().decode("utf-8", "replace")
                    header = re.match(r"\s*/\*(.*?)\*/", text, re.S)
                    if header and "Copyright" in header[1]:
                        block = "\n".join(re.sub(r"^\s*\*? ?", "", line).rstrip()
                                          for line in header[1].splitlines()).strip()
                        headers.setdefault(block, []).append(member.name)
    if found or not headers:
        return found
    target = unpacked / "licence-in-source-headers.txt"
    target.write_text("\n\n".join(
        f"From {', '.join(sorted(names))}:\n\n{block}" for block, names in sorted(headers.items())
    ) + "\n", encoding="utf-8")
    return [target]


def system(out: Path, libraries: List[Path], record: Optional[Path], sources: Optional[str]) -> None:
    section = out / "system"
    owners: Dict[str, dict] = {}
    for library in libraries:
        found, real = system_file(library.name)
        if shutil.which("rpm"):
            query = "%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{LICENSE}\t%{SOURCERPM}"
            fields = run("rpm", "-qf", "--qf", query + "\n", str(real)).splitlines()[0].split("\t")
            name, version, arch, licence, srpm = fields
            owner = owners.setdefault(name, {
                "version": version, "licence": licence, "source": srpm,
                "nevra": f"{name}-{version}.{arch}", "libraries": []})
            if "files" not in owner:
                owner["files"] = rpm_licence_files(name, owner["nevra"])
        elif shutil.which("dpkg-query"):
            name = dpkg_owner([found, real])
            version = run("dpkg-query", "-W", "-f", "${Version}", name)
            owner = owners.setdefault(name, {
                "version": version, "licence": "see copyright", "source": f"apt-get source {name}={version}",
                "nevra": f"{name}={version}", "libraries": [],
                "files": [Path("/usr/share/doc") / name / "copyright"]})
        else:
            fail("this machine has neither rpm nor dpkg to say whose its libraries are")
        owner["libraries"].append(library.name)

    rows = []
    unlicensed = []
    for name, owner in sorted(owners.items()):
        files = [f for f in owner["files"] if f.is_file()]
        if not files:
            unlicensed.append(f"{name} {owner['version']}")
            continue
        names = copy_into(section / name, [(f, f.name) for f in files])
        rows.append([name, owner["version"], owner["licence"], ", ".join(sorted(owner["libraries"])),
                     links(name, names), owner["source"]])
    if unlicensed:
        fail(f"neither these packages nor their source have a licence to carry: {', '.join(unlicensed)}")
    if record:
        with record.open("a", encoding="utf-8") as stream:
            for owner in owners.values():
                stream.write(owner["nevra"] + "\n")

    where = (f"Each package's source is published beside this image on its release page, "
             f"in {sources}, as the source RPM named here." if sources else
             "Each package's source is its distribution's, as named here.")
    write_index(
        section / "index.md",
        "System libraries",
        f"""
These libraries are the build machine's -- AlmaLinux 8's and EPEL 8's, for
an image built as a release is -- and are carried because a host cannot be
counted on to have them. Each is listed under the package that installed it,
with that package's licence files. {where}
""",
        ["Package", "Version", "Licence", "Libraries", "Files", "Source"],
        rows,
    )


# --- check -----------------------------------------------------------------
# What verify.sh and verify.ps1 ask of a finished package: that it carries a
# licences folder, and that what the folder covers is what the package
# carries. A Qt binary is a Qt library, ICU, or anything in a plugin
# directory, and under PyQt6 anything in Qt6/; a system library is any other
# library in an AppImage's usr/lib.

QT_LIBRARY = re.compile(r"^(libQt6.*\.so[.\d]*|Qt6\w+\.dll|libicu\w+\.so[.\d]*|icu\w+\.dll)$")
PYTHON = re.compile(r"^(python3(\.\d+)?|python3\d+\.dll|libpython3[.\d]*\.so[.\d]*)$")
RUST = re.compile(r"^plotcore-egui-demo(\.exe)?$")
# Microsoft's, redistributed on Microsoft's terms, wherever a bundle has it.
VC_RUNTIME = re.compile(r"(?i)^(vcruntime|msvcp|concrt|vccorlib|vcomp)")
PLUGIN_DIRS = {"platforms", "platforminputcontexts", "platformthemes", "imageformats",
               "iconengines", "styles", "generic", "networkinformation", "tls",
               "xcbglintegrations", "egldeviceintegrations", "wayland-decoration-client",
               "wayland-graphics-integration-client", "wayland-shell-integration"}


def check(root: Path, licences: Path) -> None:
    problems = []
    if not (licences / "README.md").is_file():
        problems.append(f"there is no {licences.relative_to(root)}/README.md")
    names = {p.name for p in root.rglob("*")}
    if any(PYTHON.match(n) for n in names) and not (licences / "python" / "index.md").is_file():
        problems.append("it carries a Python, and there is no Python section")
    if any(RUST.match(n) for n in names) and not (licences / "rust" / "index.md").is_file():
        problems.append("it carries the egui demo, and there is no Rust section")

    qt_names = set((licences / "qt" / "covers.txt").read_text(encoding="utf-8").split()) \
        if (licences / "qt" / "covers.txt").is_file() else set()
    for text in ("LGPL-3.0.txt", "GPL-3.0.txt"):
        if qt_names and not (licences / text).is_file():
            problems.append(f"it carries Qt, which is conveyed under the LGPL-3.0, without {text}")
    system_text = (licences / "system" / "index.md").read_text(encoding="utf-8") \
        if (licences / "system" / "index.md").is_file() else ""

    for path in root.rglob("*"):
        if not path.is_file() or not re.search(r"\.(so[.\d]*|dll)$", path.name) \
                or VC_RUNTIME.match(path.name):
            continue
        parts = path.relative_to(root).parts
        is_qt = (QT_LIBRARY.match(path.name) or PLUGIN_DIRS.intersection(parts[:-1])
                 or ("PyQt6" in parts and "Qt6" in parts))
        if is_qt:
            if path.name not in qt_names:
                problems.append(f"{path.relative_to(root)} is Qt's, and the Qt section does not name it")
        elif parts[:2] == ("usr", "lib") and len(parts) == 3:
            if path.name not in system_text:
                problems.append(f"{path.relative_to(root)} is the build machine's, and the "
                                f"system section does not name it")
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        sys.exit(1)
    print("the licences folder covers what the package carries: "
          + ", ".join(s for s in ("rust", "python", "qt", "system")
                      if (licences / s / "index.md").is_file()))


def main(argv: List[str]) -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("rust")
    p.add_argument("out", type=Path)
    p.add_argument("workspace", type=Path)
    p.add_argument("package")
    p.add_argument("target")

    p = commands.add_parser("python")
    p.add_argument("out", type=Path)
    p.add_argument("--interpreter-licences", type=Path)

    p = commands.add_parser("qt")
    p.add_argument("out", type=Path)
    p.add_argument("source", type=Path, help="Qt's sbom directory, or one this wrote")
    p.add_argument("binaries", nargs="+", help="the Qt libraries and plugins the package carries")
    p.add_argument("--lean", action="store_true",
                   help="leave out the texts kept under licenses/, to keep what is written there")

    p = commands.add_parser("system")
    p.add_argument("out", type=Path)
    p.add_argument("libraries", nargs="*", type=Path)
    p.add_argument("--record", type=Path, help="append each library's package to this file")
    p.add_argument("--sources", help="the release file the packages' source is published in")

    p = commands.add_parser("check")
    p.add_argument("root", type=Path)
    p.add_argument("licences", type=Path)

    args = parser.parse_args(argv)
    if args.command == "rust":
        rust(args.out, args.workspace, args.package, args.target)
    elif args.command == "python":
        python(args.out, args.interpreter_licences)
    elif args.command == "qt":
        qt(args.out, args.source, args.binaries, args.lean)
    elif args.command == "system":
        if args.libraries:
            system(args.out, args.libraries, args.record, args.sources)
    elif args.command == "check":
        check(args.root, args.licences)


if __name__ == "__main__":
    main(sys.argv[1:])
