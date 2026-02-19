from zipfile import ZipFile

pname = "CustomFOV"

with ZipFile(f"build/{pname}.zip", 'w') as zip:
    zip.write(f"build/bin/x64/Release/{pname}.dll", f"nativePC/plugins/{pname}.dll")
    zip.write(f"{pname}.toml", f"nativePC/plugins/{pname}.toml")
