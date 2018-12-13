# -*- coding: utf-8 -*-

import imp
import time
import os
import sys
import re
import subprocess

import bpy

from . import utils

LAYER_TEXT_BLOCK = '''#
# layer '{0}'
#
import bpy
bpy.context.scene.render.use_sequencer = False
bpy.context.scene.render.use_compositing = False
layers = bpy.context.scene.render.layers
for layer in layers:
    layer.use = False
layers['{0}'].use = True
bpy.context.scene.render.filepath = bpy.context.scene.render.filepath \
    + '_' + "{0}" + '_'
'''

CMD_TEMPLATE = "blender -b \"{blend_scene}\" -y -E {render_engine} " \
        "{python_options}" "{output_options} -s @#@ " \
        "-e @#@ -j {frame_inc} -a"


class CGRU_Browse(bpy.types.Operator):
    bl_idname = "cgru.browse"
    bl_label = "Show job"
    bl_description = "Show Render Queue in web browser"
    bl_options = {"REGISTER"}

    def execute(self, context):
        import cgruconfig
        import webbrowser

        server_address = cgruconfig.VARS['af_servername']
        server_port = cgruconfig.VARS['af_serverport']
        webbrowser.open('http://%s:%s' % (server_address, server_port))

        return {"FINISHED"}

# class CGRU_StartWatch(bpy.types.Operator):
    # bl_idname = "cgru.startwatch"
    # bl_label = "Start Watch"
    # bl_description = "Launch Afanasy Watch"
    # bl_options = {"REGISTER"}

    # def execute(self, context):
        # prefs = context.user_preferences.addons[__package__].preferences
        # path = os.path.abspath(bpy.path.abspath(prefs.cgru_location))
        # path2 = r"start\AFANASY\afwatch.cmd"
        # watch_path = os.path.join(path, path2)
        # print(watch_path)
        # if os.path.isfile(watch_path):
            # os.system('start cmd /c "%s"') % watch_path
            

        # return {"FINISHED"}


class CGRU_Submit(bpy.types.Operator):
    """Submit job to Afanasy Renderfarm"""

    bl_idname = "cgru.submit"
    bl_label = "Submit Job"
    bl_description = "Submit Job according to current settings"
    bl_options = {"REGISTER"}
    
    @classmethod
    def poll(cls, context):
        return bpy.data.is_saved==True

    def execute(self, context):
        sce = context.scene
        cgru_props = sce.cgru
        rd = context.scene.render
        images = None
        audio_file = ""
        engine_string = sce.render.engine
        sceneModified = False  # if the opriginal scene modified checker
        
        ###ajout###
        abs_filepath = os.path.abspath(bpy.path.abspath(rd.filepath))
        
        #audio mixdown
        if cgru_props.make_movie :
            if cgru_props.mov_name == '' :
                #create movie folder
                if "#" not in abs_filepath:
                    movie_folder = abs_filepath + "_movie"
                else:
                    movie_folder = abs_filepath.replace("#","").replace("_#","") + "_movie"
                if os.path.isdir(movie_folder)==False:
                    os.mkdir(movie_folder)
            else :
                movie_folder = os.path.dirname(os.path.abspath(bpy.path.abspath(cgru_props.mov_name)))
            if cgru_props.mov_audio :
                if cgru_props.mov_name!='' :
                    name = os.path.basename(os.path.abspath(bpy.path.abspath(cgru_props.mov_name)))
                else :
                    name = ""
                audio_file = create_audio_mixdown(rd, movie_folder, name)
        ###fin ajout###
        
        # Import Afanasy module:
        import af

        # Calculate temporary scene path:
        scenefile = bpy.data.filepath
        if scenefile.endswith('.blend'):
            scenefile = scenefile[:-6]
        renderscenefile = "%s.%s.blend" % (
            scenefile,
            time.strftime('%Y%m%d%H%M%S'))

        # Make all Local and pack all textures and objects
        if cgru_props.packLinkedObjects:
            bpy.ops.object.make_local(type='ALL')
            sceneModified = True
        if cgru_props.relativePaths:
            bpy.ops.file.make_paths_relative()
            sceneModified = True
        if cgru_props.packTextures:
            bpy.ops.file.pack_all()
            sceneModified = True

        # Get job name:
        jobname = cgru_props.jobname
        # If job name is empty use scene file name:
        if not jobname:
            jobname = os.path.basename(scenefile)
            # Try to cut standart '.blend' extension:
            if jobname.endswith('.blend'):
                jobname = jobname[:-6]

        # Get frames settings:
        fstart = sce.frame_start
        fend = sce.frame_end
        finc = sce.frame_step
        fpertask = cgru_props.fpertask
        sequential = cgru_props.sequential

        # Check frames settings:
        if fpertask < 1:
            fpertask = 1
        if fend < fstart:
            fend = fstart

        # Create a job:
        job = af.Job(jobname)

        servicename = 'blender'
        renderlayer_names = []
        layers = bpy.context.scene.render.layers

        if cgru_props.splitRenderLayers and len(layers) > 1:
            for layer in layers:
                if layer.use:
                    renderlayer_names.append(layer.name)
        else:
            renderlayer_names.append('')

        for renderlayer_name in renderlayer_names:
            block = None
            images = None

            # Create block
            if cgru_props.splitRenderLayers and len(layers) > 1:
                txt_block = bpy.data.texts.new("layer_%s" % renderlayer_name)
                txt_block.write(LAYER_TEXT_BLOCK.format(renderlayer_name))
                block = af.Block("layer_%s" % renderlayer_name, servicename)
            else:
                block = af.Block(engine_string, servicename)

            # Check current render engine
            if engine_string == 'BLENDER_RENDER':
                block.setParser('blender_render')
            elif engine_string == 'CYCLES':
                block.setParser('blender_cycles')

            if cgru_props.filepath != '':
                pos = cgru_props.filepath.find('#')
                if pos != -1:
                    if cgru_props.filepath[pos-1] in '._- ':
                        images = "{0}{1}{2}".format(
                            cgru_props.filepath[:pos-1],
                            renderlayer_name,
                            cgru_props.filepath[pos-1:])
                    else:
                        images = "{0}{1}{2}".format(
                            cgru_props.filepath[:pos],
                            renderlayer_name,
                            cgru_props.filepath[pos:])
                else:
                    images = "{0}{1}".format(
                        cgru_props.filepath,
                        renderlayer_name)

                output_images = re.sub(r'(#+)', r'@\1@', images)
                if output_images.startswith('//'):
                    output_images = os.path.join(
                        os.path.dirname(renderscenefile),
                        output_images.replace('//', ''))

                if rd.file_extension not in output_images:
                    block.setFiles([output_images + rd.file_extension])
                else:
                    block.setFiles([output_images])
                    
            ###ajout###
            else:
                pos = abs_filepath.find('#')
                if abs_filepath[pos-1] in '._- ':
                    images = "{0}{1}{2}".format(
                        abs_filepath[:pos-1],
                        renderlayer_name,
                        abs_filepath[pos-1:])
                else:
                    images = "{0}{1}{2}".format(
                        abs_filepath[:pos],
                        renderlayer_name,
                        abs_filepath[pos:])
            ###fin ajout###

            if cgru_props.splitRenderLayers and len(layers) > 1:
                python_options = ' --python-text "layer_%s"' % renderlayer_name
            else:
                python_options = ''
            cmd = CMD_TEMPLATE.format(
                    blend_scene=renderscenefile,
                    render_engine=engine_string,
                    python_options=python_options,
                    output_options=' -o "%s" ' % images if images else '',
                    frame_inc=finc)

            block.setCommand(cmd)
            block.setNumeric(fstart, fend, fpertask, finc)
            block.setSequential(sequential)
            
            if cgru_props.capacity != 1000:
                block.setCapacity(cgru_props.capacity)

            job.blocks.append(block)
            
            # make movie
            if cgru_props.make_movie:
                movie_block = af.Block(cgru_props.mov_name + '-movie', 'movgen')
                #error otherwise with depend mask#
                #movie_block.setDependMask(job.blocks[-1])
                movie_task = af.Task(cgru_props.mov_name)
                movie_block.tasks.append(movie_task)
                cmd = os.getenv('CGRU_LOCATION')
                cmd = os.path.join(cmd,
                                   'utilities',
                                   'moviemaker',
                                   'makemovie.py')
                cmd = 'python "%s"' % cmd
                cmd += ' --codec "%s"' % cgru_props.mov_codecs
                cmd += ' -r "%sx%s"' % (
                    cgru_props.mov_width,
                    cgru_props.mov_height)
                ###ajout###    
                
                if audio_file!="":
                    cmd += ' --audio="%s"' % audio_file
                r_path = os.path.abspath(bpy.path.abspath(rd.frame_path()))
                extension = os.path.splitext(r_path)[len(os.path.splitext(r_path))-1]
                
                cmd += ' -os "%s"' % "windows"
                
                if "#" not in images:
                    new_mov_name = os.path.join(movie_folder, os.path.basename(images))
                    images += "_####" + extension
                else:
                    simple_name = images.replace("#","").replace("_#","")
                    new_mov_name = os.path.join(movie_folder, os.path.basename(simple_name))
                    images += extension
                cmd += ' "%s"' % images.replace('@#', '#').replace('#@', '#')
                if cgru_props.mov_name != '':
                    cmd += ' "%s"' % os.path.abspath(bpy.path.abspath(cgru_props.mov_name))
                else:
                    cmd += ' "%s"' % new_mov_name
                ###fin ajout###
                #cmd += ' "%s"' % os.path.abspath(bpy.path.abspath(cgru_props.mov_name))
                movie_task.setCommand(cmd)
                job.blocks.append(movie_block)

        # Set job running parameters:
        if cgru_props.maxruntasks > -1:
            job.setMaxRunningTasks(cgru_props.maxruntasks)
        if cgru_props.maxruntasksperhost > -1:
            job.setMaxRunTasksPerHost(cgru_props.maxruntasksperhost)
        if cgru_props.priority > -1:
            job.setPriority(cgru_props.priority)
        if cgru_props.dependmask != '':
            job.setDependMask(cgru_props.dependmask)
        if cgru_props.dependmaskglobal != '':
            job.setDependMaskGlobal(cgru_props.dependmaskglobal)
        if cgru_props.hostsmask != '':
            job.setHostsMask(cgru_props.hostsmask)
        if cgru_props.hostsmaskexclude != '':
            job.setHostsMaskExclude(cgru_props.hostsmaskexclude)
        if cgru_props.pause:
            job.offLine()
        if cgru_props.previewPendingApproval:
            job.setPPApproval()
        if cgru_props.properties_needed!='':
            job.setNeedProperties(cgru_props.properties_needed)
        # Make server to delete temporary file after job deletion:
        job.setCmdPost('deletefiles "%s"' % os.path.abspath(renderscenefile))

        # Print job information:
        #job.output(True)

        # Save Temporary file
        bpy.ops.wm.save_as_mainfile(filepath=renderscenefile, copy=True)
        
        # change last saved property
        cgru_props.last_saved_file = renderscenefile
        
        # Clean up temp text blocks
        if cgru_props.splitRenderLayers and len(layers) > 1:
            for text in bpy.data.texts:
                if "layer_" in text:
                    bpy.data.texts.remove(text)

        #  Send job to server:
        result = job.send()
        if not result[0]:
            msg = (
                "An error occurred when submitting job to Afanasy."
                "Check console.")
            self.report({'ERROR'}, msg)
        else:
            msg = "Job successfully submit to Afanasy."
            self.report({'INFO'}, msg)

        # if original scene is modified - we need to reload the scene file
        if sceneModified:
            bpy.ops.wm.open_mainfile(filepath=scenefile + ".blend")
                
        return {'FINISHED'}

        
        #submit with override
class CGRU_SubmitOverride(bpy.types.Operator):
    bl_idname = "cgru.submit_override"
    bl_label = "Submit Override"
    bl_description = "Submit and Override some render settings"
    bl_options = {"REGISTER"}
    
    possible_states = [
                ("0","CPU","CPU"),
                ("1","GPU","GPU"),
                ("2","CPU and GPU","CPU and GPU"),
                ]
    device = bpy.props.EnumProperty(name="Device", default="2", items=possible_states)
    cpu_tile_x = bpy.props.IntProperty(name='X', default=32)
    cpu_tile_y = bpy.props.IntProperty(name='Y', default=32)
    gpu_tile_x = bpy.props.IntProperty(name='X', default=256)
    gpu_tile_y = bpy.props.IntProperty(name='Y', default=256)
    suffix = bpy.props.BoolProperty(name='Suffix in job name', default=True)

    @classmethod
    def poll(cls, context):
        return bpy.data.is_saved==True and bpy.context.scene.render.engine == 'CYCLES'
    
    def invoke(self, context, event):
        wm = context.window_manager
        return wm.invoke_props_dialog(self, width=300, height=100)
    
    def check(self, context):
        return True
    
    def draw(self, context):
        layout = self.layout
        layout.prop(self, "device")
        if self.device in {'0', '2'}:
            row=layout.row(align=True)
            row.label('CPU Tiles')
            row.prop(self, 'cpu_tile_x')
            row.prop(self, 'cpu_tile_y')
        if self.device in {'1', '2'}:
            row=layout.row(align=True)
            row.label('GPU Tiles')
            row.prop(self, 'gpu_tile_x')
            row.prop(self, 'gpu_tile_y')
        layout.prop(self, "suffix")
        
    def execute(self, context):
        scn = context.scene
        cgru_props = scn.cgru
        rd = scn.render
        blendname = bpy.path.basename(bpy.context.blend_data.filepath)
        
        #get old render settings
        old_jobname = scn.cgru.jobname
        old_device = scn.cycles.device
        old_tile_x = rd.tile_x
        old_tile_y = rd.tile_y
        old_overwrite = rd.use_overwrite
        old_placeholder = rd.use_placeholder
        old_hostsmask = scn.cgru.hostsmask
        old_makemovie = cgru_props.make_movie
        
        #change render settings
        
        #CPU
        if self.device=='0':
            
            scn.cycles.device = 'CPU'
            rd.tile_x = self.cpu_tile_x
            rd.tile_y = self.cpu_tile_y
            scn.cgru.hostsmask = ".*cpu"
            bpy.ops.cgru.submit()
            
        #GPU
        elif self.device=='1':
            
            scn.cycles.device = 'GPU'
            rd.tile_x = self.gpu_tile_x
            rd.tile_y = self.gpu_tile_y
            scn.cgru.hostsmask = ".*gpu"
            bpy.ops.cgru.submit()
            
        #CPU GPU
        elif self.device=='2':
            
            rd.use_overwrite = False
            rd.use_placeholder = True

            
            #cpu
            scn.cycles.device = 'CPU'
            rd.tile_x = self.cpu_tile_x
            rd.tile_y = self.cpu_tile_y
            scn.cgru.hostsmask = ".*cpu"
            if self.suffix==True:
                if old_jobname!='':
                    scn.cgru.jobname += "_cpu"
                else:
                    scn.cgru.jobname=blendname+"_cpu"
                    
            bpy.ops.cgru.submit()
            
            #wait for save and reload of the file
            time.sleep(1)
            
            if cgru_props.make_movie:
                cgru_props.make_movie = False
                            
            #gpu
            scn.cycles.device = 'GPU'
            rd.tile_x = self.gpu_tile_x
            rd.tile_y = self.gpu_tile_y
            scn.cgru.hostsmask = ".*gpu"
            if self.suffix==True:
                if old_jobname!='':
                    scn.cgru.jobname = old_jobname+"_gpu"
                else:
                    scn.cgru.jobname = blendname+"_gpu"
            
            bpy.ops.cgru.submit()
                    
        #redo render settings
        scn.cycles.device = old_device
        rd.tile_x = old_tile_x
        rd.tile_y = old_tile_y
        rd.use_overwrite = old_overwrite
        rd.use_placeholder = old_placeholder
        scn.cgru.jobname = old_jobname
        scn.cgru.hostsmask = old_hostsmask
        cgru_props.make_movie = old_makemovie
        
        msg = "Override Job(s) successfully submit to Afanasy."
        self.report({'INFO'}, msg)
                    
        return {"FINISHED"}
        
def create_audio_mixdown(rd, movie_folder, name):
    if "#" not in rd.filepath:
        filepath = os.path.abspath(bpy.path.abspath(rd.filepath))
    else:
        filepath = os.path.abspath(bpy.path.abspath(rd.filepath)).replace("#","")
    absfile = os.path.abspath(bpy.path.abspath(filepath))
    basename = os.path.basename(absfile)
    noext = os.path.splitext(basename)[len(os.path.splitext(basename))-2]
    dir = movie_folder
    if name == "" :
        temp_name = basename + '_audio_mixdown.flac'
    else :
        temp_name = name + '_audio_mixdown.flac'
    soundpath = os.path.join(dir, temp_name)
    bpy.ops.sound.mixdown(\
        filepath = soundpath,\
        check_existing = False,\
        relative_path = False,\
        accuracy = 1024,\
        container = 'FLAC',\
        codec = 'FLAC',\
        format = 'S16',\
        bitrate = 192,\
        split_channels = False)
        
    return (soundpath)