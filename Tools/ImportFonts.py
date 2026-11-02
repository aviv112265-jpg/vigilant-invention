# MISHPACHA - imports Tools/Fonts/*.ttf as Font Faces into /Game/Mishpacha/Fonts.
# The HUD looks for NotoSansHebrew-Regular / NotoSansHebrew-Bold there (UI/MishpachaText.cpp).
# Noto Sans Hebrew is under the SIL Open Font License (OFL.txt next to the fonts).
# Run (editor closed):
#   UnrealEditor-Cmd.exe "<project>\Mishpacha.uproject" -run=pythonscript -script="<project>\Tools\ImportFonts.py" -unattended -nosplash
import os
import unreal

FOLDER = "/Game/Mishpacha/Fonts"
SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Fonts")

tasks = []
for name in sorted(os.listdir(SOURCE)):
    if not name.lower().endswith((".ttf", ".otf")):
        continue
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SOURCE, name))
    task.set_editor_property("destination_path", FOLDER)
    task.set_editor_property("destination_name", os.path.splitext(name)[0])
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for task in tasks:
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log_warning(f"[Mishpacha] imported {path}")
