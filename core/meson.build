# chimera-core-gens: the Gens-Rerecording Mega Drive machine in miniBox's sandbox, as a Chimera core.
#
# The YM2612 and PSG are Gens' own C files with the audio-cursor patch applied. Make the
# patched copy first and pass it as gens_source:
#     python3 tools/prepare_patched_source.py --reference <gens checkout> --destination build/gens-source
# Native build (run-native, the same exports driven directly):
#     meson setup build/meson-native -Dgens_source=build/gens-source/Gens-rr/src && ninja -C build/meson-native
# Guest (core.wbx) build through the cross file waterbox/setup-guest.sh writes:
#     sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx
project('chimera-core-gens', 'c',
  default_options: ['c_std=gnu11', 'optimization=2', 'debug=false', 'warning_level=0'])

fs = import('fs')
gens_src = get_option('gens_source')
if gens_src == ''
  gens_src = meson.current_source_dir() / 'build/gens-source/Gens-rr/src'
endif
if not fs.is_file(gens_src / 'ym2612.c')
  error('the patched Gens source is missing: run tools/prepare_patched_source.py, or pass -Dgens_source=<patched Gens-rr/src>')
endif

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../../chimera-common-minibox'
  if fs.is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../../chimera-common-minibox)')
  endif
endif

# Gens' arithmetic must not be reassociated, contracted or narrowed differently.
add_project_arguments([
  '-fgnu89-inline', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off',
  '-fexcess-precision=standard', '-msse2', '-finput-charset=ISO-8859-1',
  '-Werror=implicit-function-declaration', '-Werror=int-to-pointer-cast',
], language: 'c')

# ym2612.c initializes const-qualified function pointer tables. GCC 14 and Clang make that an error.
add_project_arguments(meson.get_compiler('c').get_supported_arguments(
  '-Wno-error=incompatible-pointer-types', '-Wno-error=incompatible-function-pointer-types'),
  language: 'c')

inc = include_directories('waterbox')

nasm = find_program('nasm')
io_data = custom_target('gens_io_data',
  input: 'waterbox/gens_io_data_x64.asm',
  output: 'gens_io_data_x64.o',
  command: [nasm, '-f', 'elf64', '@INPUT@', '-o', '@OUTPUT@'])

core_sources = files(
  'waterbox/cinterface.c',
  'waterbox/gens_audio.c',
  'waterbox/gens_color_lookup.c',
  'waterbox/gens_genesis_bus.c',
  'waterbox/gens_gmv.c',
  'waterbox/gens_input.c',
  'waterbox/gens_io.c',
  'waterbox/gens_m68k_cpu.c',
  'waterbox/gens_m68k_tables.c',
  'waterbox/gens_machine.c',
  'waterbox/gens_pcm.c',
  'waterbox/gens_rom.c',
  'waterbox/gens_sound.c',
  'waterbox/gens_vdp.c',
  'waterbox/gens_vdp_dma.c',
  'waterbox/gens_vdp_irq.c',
  'waterbox/gens_vdp_machine.c',
  'waterbox/gens_vdp_memory_dma.c',
  'waterbox/gens_vdp_palette.c',
  'waterbox/gens_vdp_pixels.c',
  'waterbox/gens_vdp_read.c',
  'waterbox/gens_vdp_registers.c',
  'waterbox/gens_vdp_render.c',
  'waterbox/gens_vdp_reset.c',
  'waterbox/gens_vdp_scroll.c',
  'waterbox/gens_vdp_sprite_draw.c',
  'waterbox/gens_vdp_sprites.c',
  'waterbox/gens_vdp_video.c',
  'waterbox/gens_vdp_write.c',
  'waterbox/gens_z80_control.c',
  'waterbox/gens_z80_cpu.c',
  'waterbox/gens_z80_memory.c',
  'waterbox/gens_z80_native.c',
  'waterbox/gens_z80_tables.c',
  gens_src / 'ym2612.c',
  gens_src / 'psg.c',
)
gens_inc = ['-I' + gens_src]

if meson.is_cross_build()
  # ---- the guest: core.wbx ----
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')
  executable('core', core_sources, io_data,
    name_suffix: 'wbx',
    include_directories: inc,
    c_args: gens_inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax',
      '-T', mbdir / 'source/guest/linkscript.T',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lgcc', '-lc'])
else
  # ---- the native build: the same exports over shimmed emulibc ----
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'), io_data,
    include_directories: [include_directories('waterbox/native-shim'), inc,
                          include_directories('tests')],
    c_args: gens_inc + ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])
endif