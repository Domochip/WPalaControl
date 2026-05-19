from pprint import pprint
import re
from os.path import join, dirname

Import("env")

def extract_macro_value(file_path, macro_name):
    with open(file_path, 'r') as file:
        content = file.read()
        pattern = rf'#define\s+{macro_name}\s+"?([^"\s]+)"?'
        match = re.search(pattern, content)
        if match:
            return match.group(1)
        else:
            raise ValueError(f"{macro_name} not found in {file_path}")

model = extract_macro_value(r'./src/Main.h', 'CUSTOM_APP_MODEL')
version = extract_macro_value(r'./src/Main.h', 'VERSION_NUMBER')
developper_mode = extract_macro_value(r'./src/Main.h', 'DEVELOPPER_MODE')

platform = env.GetProjectOption("platform")
build_type = env.GetProjectOption("build_type", "release")

esp32suffix = ".esp32" if platform == "espressif32" else ""
developpersuffix = ".dev" if developper_mode == "1" else ""
debugsuffix = ".debug" if str(build_type).strip().lower() == "debug" else ""

env.Replace(PROGNAME=f"{model}{esp32suffix}.{version}{developpersuffix}{debugsuffix}")