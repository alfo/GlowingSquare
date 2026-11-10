# PlatformIO pre-script: passes the env's name into the firmware as APP_ENV,
# the key of this build's entry in version.json and the suffix of its
# release asset. platformio.ini cannot interpolate the env name into a
# shared [env] section itself.
Import("env")

env.Append(CPPDEFINES=[("APP_ENV", env.StringifyMacro(env["PIOENV"]))])
