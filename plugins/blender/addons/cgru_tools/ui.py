# -*- coding: utf-8 -*-
import bpy
from . import operators
from . import utils


class RENDER_PT_Afanasy(bpy.types.Panel):
    bl_label = "Afanasy"
    bl_category = 'CGRU_props'
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "render"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        cgru_props = scene.cgru
        
        row = layout.row()
        row.operator(operators.CGRU_SubmitOverride.bl_idname, text='Submit', icon='RENDER_STILL')
        
        row = layout.row()
        #row.operator(operators.CGRU_Submit.bl_idname, text='Submit Regular', icon='RENDER_STILL')
        #row.operator(operators.CGRU_StartWatch.bl_idname, text="Start Watch", icon='VIEWZOOM')
        row.operator(operators.CGRU_Browse.bl_idname, text="Show Queue", icon='VIEWZOOM')
        
        layout.separator()
        
        col = layout.column()
        col.prop(cgru_props, 'jobname')
        
        split = layout.split()
        col = split.column(align=True)
        
        col.prop(cgru_props, 'fpertask')
        
        col.prop(cgru_props, 'priority')
        
        col = split.column(align=True)
        col.prop(cgru_props, 'maxruntasksperhost')
        col.prop(cgru_props, 'pause', icon='PAUSE')
            
        row = layout.row()
        row.prop(cgru_props, 'adv_options', text='Options' ,icon='SCRIPTWIN', emboss=True)

        if cgru_props.adv_options==True:
            col = layout.column()
            col.prop(cgru_props, 'filepath')
            split = layout.split()
            col = split.column(align=True)
            col.prop(scene, 'frame_start')
            col.prop(scene, 'frame_end')
            col.prop(scene, 'frame_step')
            col = split.column(align=True)
            col.prop(cgru_props, 'capacity')
            col.prop(cgru_props, 'sequential')
            col.prop(cgru_props, 'maxruntasks')
            split = layout.split()
            col = split.column()
            col.prop(cgru_props, 'packLinkedObjects')
            col.prop(cgru_props, 'relativePaths')
            col.prop(cgru_props, 'packTextures')
            col = split.column()
            col.prop(cgru_props, 'splitRenderLayers')
            col.prop(cgru_props, 'previewPendingApproval')

            layout.separator()
            col = layout.column()
            col.prop(cgru_props, 'dependmask')
            col.prop(cgru_props, 'dependmaskglobal')
            col.prop(cgru_props, 'hostsmask')
            col.prop(cgru_props, 'hostsmaskexclude')
            col.prop(cgru_props, 'properties_needed')
            
            layout.separator()
            
        row = layout.row()
        row.prop(cgru_props, 'make_movie', icon='FILE_MOVIE')
        if cgru_props.make_movie:
            row = layout.row()
            col = row.column()
            col.prop(cgru_props, 'mov_name')
            col.prop(cgru_props, 'mov_codecs')
            split = layout.split()
            col = split.column(align=True)
            col.prop(cgru_props, 'mov_width', text='X')
            col.prop(cgru_props, 'mov_height', text='Y')
            col = split.column(align=True)
            col.prop(cgru_props, 'mov_audio', icon='PLAY_AUDIO')
            
            layout.separator()
        
        prefs = context.user_preferences.addons[__package__].preferences
        if prefs.cgru_version == utils.CGRU_NOT_FOUND:
            row.enabled = False
            layout.label(
                text="Please check CGRU location in the addon preferences.",
                icon='ERROR')
        else:
            row.enabled = True
        
        

