"""Host build of the Berry VM and scripting sources for tests-native-berry.

Only that env loads it. Shared test support (stubs, helpers) lives in
test/berry_common; the stubs must precede src/ on the include path because
they replace the ESP-IDF headers the scripting code logs through.
"""

import os

Import("env")

if env.subst("$PIOENV") == "tests-native-berry":
    project_dir = env.subst("$PROJECT_DIR")
    berry_dir = os.path.join(project_dir, "components", "berry")
    test_dir = os.path.join(project_dir, "test", "berry_common")

    env.Prepend(
        CPPPATH=[
            os.path.join(test_dir, "stubs"),
            test_dir,
            os.path.join(project_dir, "src"),
            os.path.join(berry_dir, "src"),
            os.path.join(berry_dir, "port"),
            os.path.join(berry_dir, "generate"),
        ]
    )

    # Host suites run with CWD = firmware/, so the loader's default
    # /littlefs/scripts becomes a writable relative directory.
    env.Append(CPPDEFINES=["SCRIPT_DIR_TEST"])

    env.BuildSources(
        os.path.join("$BUILD_DIR", "berry"),
        berry_dir,
        "+<src/*.c> +<port/be_modtab.c> +<port/be_port.c>",
    )
    env.BuildSources(
        os.path.join("$BUILD_DIR", "scripting"),
        os.path.join(project_dir, "src"),
        "+<scripting/berry_registry.c> +<scripting/script_loader.c> "
        "+<scripting/berry_bindings.c> +<can/msg_codec.c>",
    )
    env.BuildSources(
        os.path.join("$BUILD_DIR", "test_support"),
        test_dir,
        "+<stubs.c>",
    )
