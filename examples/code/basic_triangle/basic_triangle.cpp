#include "console.h"
#include "file_system.h"
#include "memory.h"
#include "os.h"
#include "pen.h"
#include "pen_string.h"
#include "renderer.h"
#include "threads.h"
#include "timer.h"

namespace
{
    void*  user_setup(void* params);
    loop_t user_update();
    void   user_shutdown();
} // namespace

namespace pen
{
    pen_creation_params pen_entry(int argc, char** argv)
    {
        pen::pen_creation_params p;
        p.window_width = 1280;
        p.window_height = 720;
        p.window_title = "basic_triangle";
        p.window_sample_count = 4;
        p.user_thread_function = user_setup;
        p.flags = pen::e_pen_create_flags::renderer;
        return p;
    }
} // namespace pen

namespace
{
    struct vertex
    {
        f32 x, y, z, w;
    };

    pen::job* s_thread_info = nullptr;
    u32       s_clear_state = 0;
    u32       s_raster_state = 0;
    u32       s_vertex_shader = 0;
    u32       s_pixel_shader = 0;
    u32       s_input_layout = 0;
    u32       s_vertex_buffer = 0;

    void* user_setup(void* params)
    {
        // unpack the params passed to the thread and signal to the engine it ok to proceed
        pen::job_thread_params* job_params = (pen::job_thread_params*)params;
        s_thread_info = job_params->job_info;
        pen::semaphore_post(s_thread_info->p_sem_continue, 1);

        // create clear state
        static pen::clear_state cs = {
            0.0f, 0.0, 0.5f, 1.0f, 1.0f, 0x00, PEN_CLEAR_COLOUR_BUFFER | PEN_CLEAR_DEPTH_BUFFER,
        };

        s_clear_state = pen::renderer_create_clear_state(cs);

        // create raster state
        pen::raster_state_creation_params rcp;
        pen::memory_zero(&rcp, sizeof(rcp));
        rcp.fill_mode = PEN_FILL_SOLID;
        rcp.cull_mode = PEN_CULL_NONE;
        rcp.depth_bias_clamp = 0.0f;
        rcp.sloped_scale_depth_bias = 0.0f;

        s_raster_state = pen::renderer_create_raster_state(rcp);

        // create shaders
        pen::shader_load_params vs_slp;
        vs_slp.type = PEN_SHADER_TYPE_VS;

        pen::shader_load_params ps_slp;
        ps_slp.type = PEN_SHADER_TYPE_PS;

        c8 shader_file_buf[256];

        auto platform = pen::renderer_get_shader_platform();

        pen::string_format(shader_file_buf, 256, "data/pmfx/%s/%s/%s", platform, "basictri", "default.vsc");
        pen_error err = pen::filesystem_read_file_to_buffer(shader_file_buf, &vs_slp.byte_code, vs_slp.byte_code_size);
        PEN_ASSERT(!err);

        pen::string_format(shader_file_buf, 256, "data/pmfx/%s/%s/%s", platform, "basictri", "default.psc");
        err = pen::filesystem_read_file_to_buffer(shader_file_buf, &ps_slp.byte_code, ps_slp.byte_code_size);
        PEN_ASSERT(!err);

        s_vertex_shader = pen::renderer_load_shader(vs_slp);
        s_pixel_shader = pen::renderer_load_shader(ps_slp);

        // create input layout
        pen::input_layout_creation_params ilp;
        ilp.vs_byte_code = vs_slp.byte_code;
        ilp.vs_byte_code_size = vs_slp.byte_code_size;

        ilp.num_elements = 1;

        ilp.input_layout = (pen::input_layout_desc*)pen::memory_alloc(sizeof(pen::input_layout_desc) * ilp.num_elements);

        c8 buf[16];
        pen::string_format(&buf[0], 16, "POSITION");

        ilp.input_layout[0].semantic_name = (c8*)&buf[0];
        ilp.input_layout[0].semantic_index = 0;
        ilp.input_layout[0].format = PEN_VERTEX_FORMAT_FLOAT4;
        ilp.input_layout[0].input_slot = 0;
        ilp.input_layout[0].aligned_byte_offset = 0;
        ilp.input_layout[0].input_slot_class = PEN_INPUT_PER_VERTEX;
        ilp.input_layout[0].instance_data_step_rate = 0;

        s_input_layout = pen::renderer_create_input_layout(ilp);

        // free byte code loaded from file
        pen::memory_free(vs_slp.byte_code);
        pen::memory_free(ps_slp.byte_code);

        // create vertex buffer
        vertex vertices[] = {0.0f, 0.5f, 0.5f, 1.0f, 0.5f, -0.5f, 0.5f, 1.0f, -0.5f, -0.5f, 0.5f, 1.0f};

        pen::buffer_creation_params bcp;
        bcp.usage_flags = PEN_USAGE_DEFAULT;
        bcp.bind_flags = PEN_BIND_VERTEX_BUFFER;
        bcp.cpu_access_flags = 0;

        bcp.buffer_size = sizeof(vertex) * 3;
        bcp.data = (void*)&vertices[0];

        s_vertex_buffer = pen::renderer_create_buffer(bcp);

        pen_main_loop(user_update);
        return PEN_THREAD_OK;
    }

    void user_shutdown()
    {
        PEN_LOG("User Shutdown");

        pen::renderer_new_frame();
        pen::renderer_release_clear_state(s_clear_state);
        pen::renderer_release_raster_state(s_raster_state);
        pen::renderer_release_buffer(s_vertex_buffer);
        pen::renderer_release_shader(s_vertex_shader, PEN_SHADER_TYPE_VS);
        pen::renderer_release_shader(s_pixel_shader, PEN_SHADER_TYPE_PS);
        pen::renderer_release_input_layout(s_input_layout);
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();

        // signal to the engine the thread has finished
        pen::semaphore_post(s_thread_info->p_sem_terminated, 1);
    }

    loop_t user_update()
    {
        pen::renderer_new_frame();

        // set render targets to backbuffer
        pen::renderer_set_targets(PEN_BACK_BUFFER_COLOUR, PEN_BACK_BUFFER_DEPTH);

        // clear screen
        pen::viewport vp = {0.0f, 0.0f, PEN_BACK_BUFFER_RATIO, 1.0f, 0.0f, 1.0f};

        pen::renderer_set_viewport(vp);
        pen::renderer_set_raster_state(s_raster_state);
        pen::renderer_set_scissor_rect(pen::rect{vp.x, vp.y, vp.width, vp.height});
        pen::renderer_clear(s_clear_state);

        // bind vertex layout
        pen::renderer_set_input_layout(s_input_layout);

        // bind vertex buffer
        u32 stride = sizeof(vertex);
        pen::renderer_set_vertex_buffer(s_vertex_buffer, 0, stride, 0);

        // bind shaders
        pen::renderer_set_shader(s_vertex_shader, PEN_SHADER_TYPE_VS);
        pen::renderer_set_shader(s_pixel_shader, PEN_SHADER_TYPE_PS);

        // draw
        pen::renderer_draw(3, 0, PEN_PT_TRIANGLELIST);

        // present
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();

        if (pen::semaphore_try_wait(s_thread_info->p_sem_exit))
        {
            user_shutdown();
            pen_main_loop_exit();
        }

        pen_main_loop_continue();
    }
} // namespace
