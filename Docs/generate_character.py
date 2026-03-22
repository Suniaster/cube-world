import bpy
import math

def create_blocky_character():
    # 1. Start fresh - delete existing meshes and armatures in the scene
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.context.scene.objects:
        if obj.type in {'MESH', 'ARMATURE'}:
            obj.select_set(True)
    bpy.ops.object.delete()

    # 2. Helper function to make a cube at a specific spot and scale
    def add_cube(name, location, scale):
        bpy.ops.mesh.primitive_cube_add(location=location)
        obj = bpy.context.active_object
        obj.name = name
        # The default cube is 2x2x2 meters, so we scale it down
        obj.scale = scale
        return obj

    # 3. Create the disjointed "Rayman" style blocks for procedural animation
    torso  = add_cube("Torso",  location=( 0.00,  0.0,  1.10), scale=(0.35, 0.20, 0.45))
    head   = add_cube("Head",   location=( 0.00,  0.0,  1.90), scale=(0.25, 0.25, 0.25))
    hand_l = add_cube("Hand_L", location=( 0.60,  0.0,  1.00), scale=(0.12, 0.12, 0.12))
    hand_r = add_cube("Hand_R", location=(-0.60,  0.0,  1.00), scale=(0.12, 0.12, 0.12))
    foot_l = add_cube("Foot_L", location=( 0.20,  0.0,  0.25), scale=(0.15, 0.25, 0.15))
    foot_r = add_cube("Foot_R", location=(-0.20,  0.0,  0.25), scale=(0.15, 0.25, 0.15))

    blocks = [torso, head, hand_l, hand_r, foot_l, foot_r]

    # 4. Create the Armature (Rig)
    bpy.ops.object.armature_add(enter_editmode=True, align='WORLD', location=(0, 0, 0))
    armature = bpy.context.active_object
    armature.name = "Character_Rig"
    armature.show_in_front = True # So we can see bones through the cubes
    
    # 5. Build Bones in Edit Mode
    amt = armature.data
    amt.name = "Character_Rig_Data"
    
    # The default added bone becomes our Root
    root_bone = amt.edit_bones[0]
    root_bone.name = "root"
    root_bone.head = (0, 0, 0)
    root_bone.tail = (0, 0, 0.4)

    # Helper function to add bones easily
    def add_bone(name, head_pos, tail_pos, parent=None):
        bone = amt.edit_bones.new(name)
        bone.head = head_pos
        bone.tail = tail_pos
        if parent:
            bone.parent = parent
            bone.use_connect = False
        return bone

    # Create layout of bones
    bone_pelvis = add_bone("pelvis", (0, 0, 0.65), (0, 0, 1.05), parent=root_bone)
    bone_spine  = add_bone("spine",  (0, 0, 1.05), (0, 0, 1.55), parent=bone_pelvis)
    bone_head   = add_bone("head",   (0, 0, 1.65), (0, 0, 2.15), parent=bone_spine)
    
    # Floating limb bones parented to upper spine
    bone_hand_l = add_bone("hand_l", ( 0.60, 0,  1.12), ( 0.60, 0,  0.88), parent=bone_spine)
    bone_hand_r = add_bone("hand_r", (-0.60, 0,  1.12), (-0.60, 0,  0.88), parent=bone_spine)
    
    # Floating feet parented to pelvis
    bone_foot_l = add_bone("foot_l", ( 0.20, 0,  0.40), ( 0.20, 0,  0.10), parent=bone_pelvis)
    bone_foot_r = add_bone("foot_r", (-0.20, 0,  0.40), (-0.20, 0,  0.10), parent=bone_pelvis)

    # 6. Exit Edit Mode to bind geometry
    bpy.ops.object.mode_set(mode='OBJECT')

    # 7. Parent geometry to bones with Automatic Weights
    bpy.ops.object.select_all(action='DESELECT')
    for block in blocks:
        block.select_set(True)
    armature.select_set(True)
    
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    
    print("Character generated and rigged successfully!")

# Run it
create_blocky_character()
