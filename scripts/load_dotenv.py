Import("env")

import os

dotenv_path = os.path.join(env["PROJECT_DIR"], "src", ".env")

if os.path.isfile(dotenv_path):
    defines = []
    with open(dotenv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            defines.append(("ENV_" + key, '\\"%s\\"' % value))
    env.Append(CPPDEFINES=defines)
    print("[load_dotenv] injected %d value(s) from src/.env" % len(defines))
else:
    print("[load_dotenv] warning: %s not found, ENV_* defines will be missing" % dotenv_path)
