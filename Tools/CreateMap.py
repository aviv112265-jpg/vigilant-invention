# MISHPACHA - creates /Game/Maps/TelAvir_Greybox from the engine's Basic level template.
# The level is just a floor, sun and sky: the game mode builds the gray-box city, the family,
# missions, cars and the day/night cycle at runtime (see AMishpachaGameModeBase::EnsureAutoContent).
#
# Run (editor closed):
#   UnrealEditor-Cmd.exe "<project>\Mishpacha.uproject" -run=pythonscript -script="<project>\Tools\CreateMap.py" -unattended -nosplash

import unreal

MAP_PATH = "/Game/Maps/TelAvir_Greybox"
TEMPLATE = "/Engine/Maps/Templates/Template_Default"

if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    unreal.log_warning(f"[Mishpacha] {MAP_PATH} already exists - left as is.")
else:
    world = unreal.EditorLoadingAndSavingUtils.new_map_from_template(TEMPLATE, False)
    if not world:
        raise RuntimeError("could not create a level from " + TEMPLATE)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError("could not save " + MAP_PATH)
    unreal.log_warning(f"[Mishpacha] created {MAP_PATH}")
