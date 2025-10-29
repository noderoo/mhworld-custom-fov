from zipfile import ZipFile

pname = "CustomFOV"

with ZipFile(f"x64/{pname}.zip", 'w') as zip:
    zip.write(f"x64/Release/{pname}.dll", f"nativePC/plugins/{pname}.dll")
    zip.write(f"{pname}.toml", f"nativePC/plugins/{pname}.toml")
