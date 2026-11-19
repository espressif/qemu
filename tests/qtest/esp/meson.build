# ESP C qtests are auto-discovered from *-test.c and *_test.c files and
# linked into a single esp-qtest runner via ESP_QTEST_SUITE() constructors.
# Adding a new test file requires no meson.build edits (re-run
# `meson setup --reconfigure build` if an existing build directory is reused).

esp_qtest_targets = []
foreach dir : target_dirs
  if dir in ['xtensa-softmmu', 'riscv32-softmmu']
    esp_qtest_targets += [dir.split('-')[0]]
  endif
endforeach

if esp_qtest_targets.length() > 0
  esp_qtest_files = run_command(
    python, '-c',
    'import os, sys; print("\\n".join(sorted(f for f in os.listdir(sys.argv[1]) if f.endswith(("-test.c", "_test.c")))))',
    meson.current_source_dir(),
    check: true,
  ).stdout().strip()

  esp_qtest_srcs = files('esp-qtest.c')
  if esp_qtest_files != ''
    esp_qtest_srcs += files(esp_qtest_files.split('\n'))
  endif

  esp_qtest = executable(
    'esp-qtest',
    esp_qtest_srcs,
    include_directories: include_directories('..'),
    dependencies: [qemuutil, qos],
  )

  foreach target_base : esp_qtest_targets
    esp_emulator = emulators['qemu-system-' + target_base]
    esp_deps = roms + [esp_emulator, emulator_modules]
    esp_env = environment()
    esp_env.set('QTEST_QEMU_BINARY', './qemu-system-' + target_base)
    if have_tools
      esp_env.set('QTEST_QEMU_IMG', './qemu-img')
      esp_deps += [qemu_img]
    endif
    esp_env.set('PYTHON', python.full_path())

    test('qtest-@0@/esp-qtest'.format(target_base),
         esp_qtest,
         depends: esp_deps,
         env: esp_env,
         args: ['--tap', '-k'],
         protocol: 'tap',
         timeout: 60,
         suite: ['qtest', 'qtest-' + target_base, 'esp-qtest'])
  endforeach
endif
