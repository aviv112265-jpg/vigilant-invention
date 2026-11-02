# MISHPACHA - builds the gray-box city's procedural materials (no textures needed).
#
# Every surface is computed in a pixel shader from the world position, so it costs a few
# ALU ops instead of texture memory, and it never stretches on the scaled gray-box cubes.
#
# Run (editor closed):
#   UnrealEditor-Cmd.exe "<project>\Mishpacha.uproject" -run=pythonscript -script="<project>\Tools\CreateMaterials.py" -unattended -nosplash
# Output: /Game/Mishpacha/Materials/M_*  (ACityGreyboxGenerator picks them up automatically).

import unreal

FOLDER = "/Game/Mishpacha/Materials"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

# Shared HLSL: cheap value noise. HASH(p) and NOISE(p) are written out inline per use.
HASH = "frac(sin(dot({p}, float2(127.1, 311.7))) * 43758.5453)"


def noise(p, var):
    """Inline 2D value noise of expression p into float var."""
    return f"""
    float2 {var}_i = floor({p});
    float2 {var}_f = frac({p});
    {var}_f = {var}_f * {var}_f * (3.0 - 2.0 * {var}_f);
    float {var} = lerp(lerp({HASH.format(p=var + '_i')}, {HASH.format(p=var + '_i + float2(1, 0)')}, {var}_f.x),
                       lerp({HASH.format(p=var + '_i + float2(0, 1)')}, {HASH.format(p=var + '_i + float2(1, 1)')}, {var}_f.x), {var}_f.y);
"""


# Street-lamp light pools, matching the lamps ACityGreyboxGenerator places: on both curbs of every
# road, LampSpacing apart, LampPhase after each intersection. Writes float "pool" (0..1).
LAMP_POOLS = """
float2 rel = WP.xy - GridOrigin.xy;
float dxa = rel.x - round(rel.x / GridStep) * GridStep;
float la_x = abs(abs(dxa) - LampOffset);
float la_y = abs(frac((rel.y - LampPhase) / LampSpacing + 0.5) - 0.5) * LampSpacing;
float dyb = rel.y - round(rel.y / GridStep) * GridStep;
float lb_y = abs(abs(dyb) - LampOffset);
float lb_x = abs(frac((rel.x - LampPhase) / LampSpacing + 0.5) - 0.5) * LampSpacing;
float pool = max(exp(-(la_x * la_x + la_y * la_y) / 90000.0), exp(-(lb_x * lb_x + lb_y * lb_y) / 90000.0));
"""
LAMP_INPUTS = ["Night", "GridOrigin", "GridStep", "LampOffset", "LampSpacing", "LampPhase"]
LAMP_COLOR = "float3(1.0, 0.72, 0.42)"
# Defaults match the generator's defaults (it overrides them per city anyway); never 0 (divisions).
LAMP_DEFAULTS = {"GridStep": 7600.0, "LampOffset": 760.0, "LampSpacing": 1900.0, "LampPhase": 950.0}

FACADE = """
float3 wp = WP;
float3 n = normalize(N);
Roughness = 0.85;
Metallic = 0.0;
Emissive = float3(0, 0, 0);
// Per-building paint from the instance (custom data 0..2); the component's Color when unset.
float3 tint = dot(CD, float3(1, 1, 1)) > 0.01 ? CD : Color.rgb;
float z = wp.z - GroundZ;

// Roof: worn concrete.
if (n.z > 0.7)
{
""" + noise("wp.xy / 140.0", "rn") + """
    return float3(0.40, 0.39, 0.37) * (0.82 + 0.25 * rn);
}

// Along-the-wall coordinate (walls face X or Y).
float h = abs(n.x) > 0.5 ? wp.y : wp.x;

if (Tower > 0.5)
{
    // Curtain wall: glass panels 150 x 400 with aluminium mullions.
    float2 g = float2(h / 150.0, z / 400.0);
    float2 cell = floor(g);
    float2 uv = frac(g);
    float r = """ + HASH.format(p="cell") + """;
    bool mullion = uv.x < 0.045 || uv.y < 0.06;
    Roughness = mullion ? 0.45 : 0.06;
    Metallic = mullion ? 0.85 : 0.25;
    // Offices left on at night.
    Emissive = (!mullion && r > 0.55) ? float3(0.85, 0.85, 0.75) * Night * (0.8 + r) : float3(0, 0, 0);
    return mullion ? float3(0.52, 0.55, 0.58) : tint * float3(0.32, 0.42, 0.52) * (0.75 + 0.45 * r);
}

// Bauhaus plaster: soft blotches + fine grain, a little grime near the street.
""" + noise("float2(h, z) / 220.0", "pn") + noise("float2(h, z) / 18.0", "gn") + """
float3 col = tint * (0.90 + 0.12 * pn - 0.05 * gn);
col *= lerp(0.80, 1.0, saturate(z / 260.0));

// Windows on every 320 cm floor, above the 115 cm pilotis base, every 300 cm along the wall.
float fz = z - 115.0;
float fv = frac(fz / 320.0);
float fu = frac(h / 300.0);
if (fz > 0.0)
{
    if (fv > 0.30 && fv < 0.78 && fu > 0.22 && fu < 0.78)
    {
        float2 cell = floor(float2(h / 300.0, fz / 320.0));
        float r = """ + HASH.format(p="cell") + """;
        bool frame = abs(fu - 0.5) > 0.255 || abs(fv - 0.54) > 0.215;
        if (frame)
        {
            col = float3(0.86, 0.86, 0.83);
            Roughness = 0.6;
        }
        else if (r > 0.72)
        {
            // Closed "trissim" shutter.
            col = float3(0.62, 0.58, 0.50) * (0.9 + 0.1 * step(0.5, frac(fz / 9.0)));
            Roughness = 0.7;
        }
        else
        {
            col = float3(0.035, 0.05, 0.065) + 0.04 * r;
            Roughness = 0.08;
            Metallic = 0.15;
            // About half the flats have the lights on at night (warm, a bit different each).
            if (r < 0.5)
            {
                Emissive = float3(1.0, 0.75, 0.45) * Night * (1.2 + 2.4 * r);
            }
        }
    }
    else if (fv > 0.24 && fv < 0.30 && fu > 0.18 && fu < 0.82)
    {
        // Sill / balcony ledge shadow.
        col *= 0.72;
    }
}

// Street-level shops (custom data 3): a lit shop window under a colored sign, every 600 cm.
if (CD3 > 0.5 && z < 330.0)
{
    float su = frac(h / 600.0);
    float2 scell = floor(float2(h / 600.0, wp.z / 5000.0));
    float sr = """ + HASH.format(p="scell") + """;
    float3 signs[6] = { float3(0.85, 0.12, 0.10), float3(0.05, 0.35, 0.75), float3(0.95, 0.70, 0.05),
                        float3(0.10, 0.55, 0.25), float3(0.90, 0.35, 0.55), float3(0.95, 0.45, 0.08) };
    float3 sign = signs[(int)floor(sr * 5.99)];
    bool pillar = su < 0.06 || su > 0.94;
    if (pillar || z < 15.0)
    {
        col = tint * 0.8;
    }
    else if (z > 255.0)
    {
        // Sign board with a light strip of "lettering".
        float letters = step(0.5, frac(h / 35.0)) * step(abs(z - 292.0), 9.0) * step(0.15, su) * step(su, 0.85);
        col = lerp(sign, float3(0.97, 0.95, 0.9), letters);
        Roughness = 0.5;
        Emissive += col * (0.15 + 2.5 * Night);
    }
    else if (z > 240.0)
    {
        col = float3(0.08, 0.08, 0.09);
    }
    else
    {
        // Shop glass: the goods inside are a blur of the sign's color.
""" + noise("float2(h, z) / 40.0", "goods") + """
        float door = step(abs(su - 0.5), 0.09);
        col = lerp(float3(0.05, 0.06, 0.07), sign * 0.35 + 0.05, (1.0 - door) * goods * 0.6);
        Roughness = 0.05;
        Metallic = 0.2;
        Emissive += lerp(float3(0.02, 0.02, 0.02), float3(1.0, 0.85, 0.6) * 1.6, Night) * (0.3 + 0.7 * goods) * (1.0 - door * 0.8);
    }
}
return col;
"""

ASPHALT = """
float2 p = WP.xy;
""" + noise("p / 35.0", "fine") + noise("p / 700.0", "patch") + LAMP_POOLS + """
Roughness = 0.78 + 0.12 * fine;
Metallic = 0.0;
float3 col = float3(0.028, 0.029, 0.032) * (0.75 + 0.45 * fine) * (0.8 + 0.4 * patch);
Emissive = col * """ + LAMP_COLOR + """ * pool * Night * 7.0;
return col;
"""

SIDEWALK = """
float3 n = normalize(N);
Roughness = 0.75;
Metallic = 0.0;
""" + LAMP_POOLS + """
Emissive = float3(0, 0, 0);
if (n.z < 0.5)
{
    // Curb face (Israeli curbs are often painted, keep it a plain light concrete here).
    Emissive = float3(0.38, 0.37, 0.35) * """ + LAMP_COLOR + """ * pool * Night * 6.0;
    return float3(0.38, 0.37, 0.35);
}
float2 t = WP.xy / 60.0;
float2 cell = floor(t);
float2 uv = frac(t);
float r = """ + HASH.format(p="cell") + """;
bool grout = uv.x < 0.035 || uv.y < 0.035;
""" + noise("WP.xy / 300.0", "dirt") + """
float3 tile = float3(0.30, 0.29, 0.27) * (0.88 + 0.2 * r) * (0.88 + 0.16 * dirt);
tile = grout ? tile * 0.55 : tile;
Emissive = tile * """ + LAMP_COLOR + """ * pool * Night * 6.0;
return tile;
"""

LAMP_GLOW = """
Roughness = 0.4;
Metallic = 0.0;
Emissive = """ + LAMP_COLOR + """ * lerp(0.02, 12.0, Night);
return float3(0.9, 0.85, 0.7);
"""

SAND = """
float2 p = WP.xy;
""" + noise("p / 90.0", "dune") + noise("p / 12.0", "grain") + """
float ripple = sin(p.x / 38.0 + dune * 7.0) * 0.03;
Roughness = 0.95;
Metallic = 0.0;
return float3(0.80, 0.69, 0.50) * (0.88 + 0.14 * dune + 0.06 * grain + ripple);
"""

SEA = """
float2 p = WP.xy;
""" + noise("p / 450.0 + float2(Time * 0.04, Time * 0.025)", "swell") + noise("p / 60.0 - float2(Time * 0.15, 0.0)", "chop") + """
Roughness = 0.05 + 0.08 * chop;
Metallic = 0.0;
return float3(0.015, 0.10, 0.17) * (0.8 + 0.35 * swell) + float3(0.0, 0.02, 0.02) * chop;
"""


# Painted street furniture (awnings, kiosks, benches, umbrellas, signs): color per instance
# (custom data 0..2, else Color), custom data 3 = white stripes, 4 = glows at night.
PAINT = """
float3 c = dot(CD, float3(1, 1, 1)) > 0.01 ? CD : Color.rgb;
if (CD3 > 0.5 && frac((WP.x + WP.y + WP.z * 0.3) / 55.0) < 0.5)
{
    c = float3(0.95, 0.94, 0.9);
}
Roughness = 0.6;
Metallic = 0.0;
Emissive = c * CD4 * Night * 3.0;
return c;
"""

# Car head / tail / brake lights: the vehicle sets Glow every frame.
CAR_LIGHT = """
Roughness = 0.15;
Metallic = 0.0;
Emissive = Color.rgb * Glow;
return Color.rgb * 0.35;
"""

# The Sela siblings' clothes, painted on the Third Person mannequin from its rest-pose (pre-skinned)
# vertex positions - no textures. Mesh space: Z up from the feet, the character faces +Y, X = sideways.
# HeightScale brings the mesh to a 180 cm reference body; all the numbers below are on that body.
OUTFIT = """
float3 p = PSP * HeightScale;
float ax = abs(p.x);
Roughness = 0.8;
Metallic = 0.0;
""" + noise("p.xz / 1.5", "weave") + """
float3 skin = Skin.rgb;
bool arm = ax > 19.5 && p.z > 84.0 && p.z < 150.0;

// ---- Head and neck ----
if (p.z > 153.0 && ax < 10.5 && !arm)
{
    float3 c = skin;
    Roughness = 0.55;
    bool hair;
    if (HairStyle < 0.5)
    {
        // Classic men's cut: short sides, a little volume on top.
        hair = p.z > 176.5 || (p.y < -1.0 && p.z > 166.0) || (ax > 7.0 && p.z > 171.0 && p.y < 5.0);
    }
    else if (HairStyle < 1.5)
    {
        // Long bob ("kare"): down to the jaw at the back and sides, side-swept fringe.
        hair = p.z > 175.0 || (p.y < 2.0 && p.z > 156.0) || (ax > 6.5 && p.z > 158.0 && p.y < 7.5)
            || (p.y > 0.0 && p.z > 173.5 + 1.5 * saturate(p.x / 6.0));
    }
    else
    {
        // Buzz cut.
        hair = p.z > 177.0 || (p.y < -1.0 && p.z > 168.0) || (ax > 7.5 && p.z > 172.0 && p.y < 4.0);
    }
    if (hair)
    {
        """ + noise("float2(p.x * 3.0, p.z * 0.6)", "strand") + """
        c = Hair.rgb * (0.75 + 0.45 * strand);
        c = HairStyle > 1.5 ? lerp(skin, c, 0.8) : c;
        Roughness = 0.45;
    }
    else if (p.y > 5.0)
    {
        // Face: eyes, brows, mouth.
        float eye = length(float2(ax - 3.3, p.z - 171.5));
        if (eye < 0.85)
        {
            c = eye < 0.45 ? float3(0.02, 0.02, 0.02) : float3(0.9, 0.9, 0.88);
            Roughness = 0.2;
        }
        else if (abs(p.z - 174.0) < 0.4 && ax > 1.6 && ax < 5.2)
        {
            c = Hair.rgb * 0.7;
        }
        else if (abs(p.z - 165.6) < 0.35 && ax < 1.9)
        {
            c = skin * float3(0.7, 0.45, 0.42);
        }
    }
    return c;
}

// ---- Arms ----
if (arm)
{
    float3 c = skin;
    Roughness = 0.6;
    if (TankTop < 0.5 && ax < SleeveEnd)
    {
        c = (JacketOn > 0.5 ? Jacket.rgb : Top.rgb) * (0.92 + 0.12 * weave);
        Roughness = 0.85;
    }
    else if (JacketOn > 0.5 && ax < SleeveEnd + 1.8)
    {
        c = Top.rgb; // shirt cuff
    }
    return c;
}

// ---- Torso ----
if (p.z > 97.0 || (JacketOn > 0.5 && p.z > 84.0))
{
    float3 c = Top.rgb * (0.94 + 0.1 * weave);
    if (TankTop > 0.5 && ((p.z > 141.0 && ax > 7.5) || (p.z > 145.0 && p.y > 0.0 && ax < 6.5)))
    {
        c = skin; // bare shoulders, scoop neck
        Roughness = 0.6;
    }
    if (JacketOn > 0.5)
    {
        bool opening = p.y > 0.0 && p.z > 115.0 && ax < (p.z - 115.0) * 0.2;
        c = opening ? Top.rgb : Jacket.rgb * (0.9 + 0.14 * weave);
        if (opening && Tie > 0.5 && ax < 1.5 + (p.z < 140.0 ? 0.6 : 0.0) && p.z < 152.0)
        {
            c = Accent.rgb; // tie
            Roughness = 0.4;
        }
        if (!opening && p.y > 0.0 && ax < 0.5 && p.z < 116.0 && frac(p.z / 7.0) < 0.12)
        {
            c = float3(0.05, 0.05, 0.05); // buttons
        }
    }
    return c;
}

// ---- Legs ----
float3 legs = Pants.rgb * (0.9 + 0.14 * weave);
if (Stripes > 0.5)
{
    // Loud swim shorts: big blotches of the accent color with white stripes.
""" + noise("p.xz / 6.0", "print") + """
    legs = lerp(Pants.rgb, Accent.rgb, step(0.55, print));
    legs = frac((p.z + p.x * 0.3) / 6.0) < 0.12 ? float3(0.95, 0.95, 0.92) : legs;
}
if (p.z < PantsEnd)
{
    legs = skin;
    Roughness = 0.6;
}
if (FlipFlops > 0.5 ? p.z < 2.2 : p.z < 10.0)
{
    legs = Shoes.rgb;
    Roughness = 0.35;
}
return legs;
"""

VECTOR_PARAMS = ("Color", "GridOrigin", "Skin", "Hair", "Top", "Jacket", "Pants", "Shoes", "Accent")
OUTFIT_DEFAULTS = {"HeightScale": 1.0, "SleeveEnd": 46.0, "PantsEnd": 10.0}


def make_collection():
    """MPC_Mishpacha: scalar "Night" (0 day .. 1 night), driven by AMishpachaDayNight."""
    path = f"{FOLDER}/MPC_Mishpacha"
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    collection = asset_tools.create_asset("MPC_Mishpacha", FOLDER, unreal.MaterialParameterCollection,
                                          unreal.MaterialParameterCollectionFactoryNew())
    night = unreal.CollectionScalarParameter()
    night.set_editor_property("parameter_name", "Night")
    night.set_editor_property("default_value", 0.0)
    collection.set_editor_property("scalar_parameters", [night])
    eal.save_asset(path, only_if_is_dirty=False)
    return collection


COLLECTION = make_collection()


def make_material(name, code, inputs, color=None, scalars=None, emissive=False, skeletal=False, vectors=None):
    path = f"{FOLDER}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    # The gray-box city draws everything with instanced static meshes.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    if skeletal:
        material.set_editor_property("used_with_skeletal_mesh", True)

    custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -350, 0)
    custom.set_editor_property("code", code)
    custom.set_editor_property("description", name)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom_inputs = []
    for input_name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    outputs = []
    extra = [("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("Metallic", unreal.CustomMaterialOutputType.CMOT_FLOAT1)]
    if emissive:
        extra.append(("Emissive", unreal.CustomMaterialOutputType.CMOT_FLOAT3))
    for output_name, output_type in extra:
        output = unreal.CustomOutput()
        output.set_editor_property("output_name", output_name)
        output.set_editor_property("output_type", output_type)
        outputs.append(output)
    custom.set_editor_property("additional_outputs", outputs)

    y = -300
    for input_name in inputs:
        if input_name == "WP":
            node = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -800, y)
        elif input_name == "N":
            node = mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -800, y)
        elif input_name == "Time":
            node = mel.create_material_expression(material, unreal.MaterialExpressionTime, -800, y)
        elif input_name == "PSP":
            # Vertex-shader only: hand it to the pixel shader through an interpolator.
            source = mel.create_material_expression(material, unreal.MaterialExpressionPreSkinnedPosition, -1100, y)
            node = mel.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -800, y)
            mel.connect_material_expressions(source, "", node, "VS")
        elif input_name == "CD":
            node = mel.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -800, y)
            node.set_editor_property("data_index", 0)
        elif input_name.startswith("CD"):
            node = mel.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -800, y)
            node.set_editor_property("data_index", int(input_name[2:]))
        elif input_name == "Night":
            node = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, -800, y)
            node.set_editor_property("collection", COLLECTION)
            node.set_editor_property("parameter_name", "Night")
        elif input_name in VECTOR_PARAMS:
            node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -800, y)
            node.set_editor_property("parameter_name", input_name)
            node.set_editor_property("default_value", (color if input_name == "Color" else None) or (vectors or {}).get(input_name) or unreal.LinearColor(1, 1, 1, 1))
        else:
            node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -800, y)
            node.set_editor_property("parameter_name", input_name)
            node.set_editor_property("default_value", (scalars or {}).get(input_name, 0.0))
        mel.connect_material_expressions(node, "", custom, input_name)
        y += 120

    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(custom, "Roughness", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(custom, "Metallic", unreal.MaterialProperty.MP_METALLIC)
    if emissive:
        mel.connect_material_property(custom, "Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(material)
    eal.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"[Mishpacha] created {path}")


make_material("M_Facade", FACADE, ["WP", "N", "Color", "CD", "CD3", "GroundZ", "Tower", "Night"], unreal.LinearColor(0.85, 0.85, 0.82, 1), emissive=True)
make_material("M_Asphalt", ASPHALT, ["WP"] + LAMP_INPUTS, scalars=LAMP_DEFAULTS, emissive=True)
make_material("M_Sidewalk", SIDEWALK, ["WP", "N"] + LAMP_INPUTS, scalars=LAMP_DEFAULTS, emissive=True)
make_material("M_Sand", SAND, ["WP"])
make_material("M_Sea", SEA, ["WP", "Time"])
make_material("M_LampGlow", LAMP_GLOW, ["Night"], emissive=True)

# Police light bar half: flashes Color twice a second, Phase 0.5 = the other half's beat.
SIREN = """
Roughness = 0.3;
Metallic = 0.0;
float flash = step(0.5, frac(Time * 2.5 + Phase));
Emissive = Color.rgb * (0.3 + 40.0 * flash);
return Color.rgb * 0.4;
"""
make_material("M_Siren", SIREN, ["Color", "Phase", "Time"], unreal.LinearColor(1, 0.05, 0.02, 1), emissive=True)
unreal.log("[Mishpacha] materials done")
make_material("M_Paint", PAINT, ["WP", "Color", "CD", "CD3", "CD4", "Night"], emissive=True)
make_material("M_CarLight", CAR_LIGHT, ["Color", "Glow"], unreal.LinearColor(1, 0.9, 0.75, 1), emissive=True)
make_material("M_Outfit", OUTFIT, ["PSP", "Skin", "Hair", "Top", "Jacket", "Pants", "Shoes", "Accent",
                                   "HeightScale", "HairStyle", "JacketOn", "SleeveEnd", "TankTop", "PantsEnd", "Stripes", "FlipFlops", "Tie"],
              scalars=OUTFIT_DEFAULTS, skeletal=True,
              vectors={"Skin": unreal.LinearColor(0.62, 0.42, 0.3, 1), "Hair": unreal.LinearColor(0.02, 0.015, 0.01, 1),
                       "Accent": unreal.LinearColor(0, 0, 0, 0)})
unreal.log("[Mishpacha] outfit materials done")
