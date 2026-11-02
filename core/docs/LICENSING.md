# Source licensing status

The project requester identifies the pinned Gens codebase as GPLv2 and
authorizes copying and modification. This is the stated project basis, not
proof that every component in the tree carries identical terms. Preserve
the original notices and include appropriate source and build instructions
when a distributable is eventually prepared.

The pinned tree has no top-level license notice. Individual files and
libraries have their own terms. For example, `Gens-rr\src\base64.c`
contains a GPL version 2-or-later notice, and
`Gens-rr\dependencies\lua\COPYRIGHT.txt` contains Lua's notice. These
individual notices do not establish the terms of every other file.

Both `Gens-rr\Starscream\Main68k\STARDOC.TXT` and
`Gens-rr\Starscream\Sub68k\STARDOC.TXT` specify unmodified redistribution,
additional restrictions, and a required credit. `Star.c` refers readers to
that notice. The proposed x86-64 port may need changes to the generator and
its generated code. Resolve the applicable permission and compatibility of
these terms before redistributing affected modifications or claiming that
the combined artifact is GPLv2 alone.

`waterbox\package-licenses.json` inventories the package's components: the
MIT integration, the Gens-Rerecording code under GPLv2 as identified by the
requester, and the 68000 core translated from Starscream's output under
Starscream's terms. The package carries Starscream's documentation, which
includes the required credit. Whether a translated Starscream core may be
redistributed has not been settled. The manifest marks commercial use as
prohibited and says not to publish the package until this is resolved.
The legal review itself is not complete.
