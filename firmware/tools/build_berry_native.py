"""Host build of the Berry VM and the script registry for tests-native-berry.

Only that env loads it. The host stubs must precede src/ on the include path,
because they replace the ESP-IDF headers the registry logs through.
"""

import os

Import("env")

if env.subst("$PIOENV") == "tests-native-berry":
    project_dir = env.subst("$PROJECT_DIR")
    berry_dir = os.path.join(project_dir, "components", "berry")

    env.Prepend(
        CPPPATH=[
            os.path.join(project_dir, "test", "test_berry_timers", "stubs"),
            os.path.join(project_dir, "src"),
            os.path.join(berry_dir, "src"),
            os.path.join(berry_dir, "port"),
            os.path.join(berry_dir, "generate"),
        ]
    )

    env.BuildSources(
        os.path.join("$BUILD_DIR", "berry"),
        berry_dir,
        "+<src/*.c> +<port/be_modtab.c> +<port/be_port.c>",
    )
    env.BuildSources(
        os.path.join("$BUILD_DIR", "scripting"),
        os.path.join(project_dir, "src"),
        "+<scripting/berry_registry.c>",
    )
