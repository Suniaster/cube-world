import bpy
import math

def create_blocky_character():
    # 1. Start fresh - delete existing meshes and armatures in the scene
    # Using low-level API to avoid context errors when run from Text Editor
    objs_to_delete = [obj for obj in bpy.context.scene.objects if obj.type in {'MESH', 'ARMATURE'}]
    for obj in objs_to_delete:
        bpy.data.objects.remove(obj, do_unlink=True)

    # 2. Helper function to make a cube at a specific spot and scale
    def add_cube(name, location, scale):
        bpy.ops.mesh.primitive_cube_add(location=location)
        obj = bpy.context.active_object
        obj.name = name
        # The default cube is 2x2x2 meters, so we scale it down
        obj.scale = scale
        return obj

    # 3. Create the disjointed blocks with Cube World style proportions
    # Big cubic head, blocky body, small chunky limbs
    torso  = add_cube("Torso",  location=( 0.00,  0.0,  0.80), scale=(0.40, 0.30, 0.40))
    head   = add_cube("Head",   location=( 0.00,  0.0,  1.70), scale=(0.50, 0.50, 0.50))
    hand_l = add_cube("Hand_L", location=( 0.60,  0.0,  0.90), scale=(0.20, 0.20, 0.20))
    hand_r = add_cube("Hand_R", location=(-0.60,  0.0,  0.90), scale=(0.20, 0.20, 0.20))
    foot_l = add_cube("Foot_L", location=( 0.20,  0.0,  0.20), scale=(0.20, 0.30, 0.20))
    foot_r = add_cube("Foot_R", location=(-0.20,  0.0,  0.20), scale=(0.20, 0.30, 0.20))

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
    bone_pelvis = add_bone("pelvis", (0, 0, 0.40), (0, 0, 0.80), parent=root_bone)
    bone_spine  = add_bone("spine",  (0, 0, 0.80), (0, 0, 1.20), parent=bone_pelvis)
    bone_head   = add_bone("head",   (0, 0, 1.20), (0, 0, 1.70), parent=bone_spine)
    
    # Floating limb bones parented to upper spine
    bone_hand_l = add_bone("hand_l", ( 0.60, 0,  1.00), ( 0.60, 0,  0.80), parent=bone_spine)
    bone_hand_r = add_bone("hand_r", (-0.60, 0,  1.00), (-0.60, 0,  0.80), parent=bone_spine)
    
    # Floating feet parented to pelvis
    bone_foot_l = add_bone("foot_l", ( 0.20, 0,  0.40), ( 0.20, 0,  0.10), parent=bone_pelvis)
    bone_foot_r = add_bone("foot_r", (-0.20, 0,  0.40), (-0.20, 0,  0.10), parent=bone_pelvis)

    # 6. Exit Edit Mode to bind geometry
    bpy.ops.object.mode_set(mode='OBJECT')

    # 7. Parent geometry to bones directly (Rigid, no deformation)
    # This prevents automatic weights from blending and stretching blocks when posing.
    bone_mapping = {
        torso: "spine", # Torso block will follow the spine
        head: "head",
        hand_l: "hand_l",
        hand_r: "hand_r",
        foot_l: "foot_l",
        foot_r: "foot_r"
    }

    for block, bone_name in bone_mapping.items():
        # Parent the block purely to its specific bone to keep it perfectly rigid
        orig_matrix = block.matrix_world.copy()
        block.parent = armature
        block.parent_type = 'BONE'
        block.parent_bone = bone_name
        block.matrix_world = orig_matrix
    
    print("Character generated and rigged successfully!")

# Run it
create_blocky_character()
