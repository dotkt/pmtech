#include "dev_ui.h"
#include "file_system.h"
#include "loader.h"
#include "memory.h"
#include "pen.h"
#include "pen_string.h"
#include "pmfx.h"
#include "renderer.h"
#include "stb/stb_image_write.h"
#include "threads.h"
#include "timer.h"

using namespace pen;
using namespace put;

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
        p.window_title = "texture_formats";
        p.window_sample_count = 4;
        p.user_thread_function = user_setup;
        p.flags = pen::e_pen_create_flags::renderer;
        return p;
    }
} // namespace pen

namespace
{
    struct typed_texture
    {
        const c8* fromat;
        u32       handle;
    };

    job_thread_params* s_job_params = nullptr;
    job*               s_thread_info = nullptr;
    const c8**         s_texture_formats = nullptr;
    typed_texture*     s_textures = nullptr;
    u32                s_num_textures = 0;
    u32                s_clear_state = 0;

    template <typename T>
    u32 create_float_texture(u32 w, u32 h, texture_format fmt, T r, T g, T b, T a)
    {
        pen::texture_creation_params tcp;
        tcp.collection_type = pen::TEXTURE_COLLECTION_NONE;

        u32 data_size = w * h * 4 * sizeof(T);

        tcp.width = w;
        tcp.height = h;
        tcp.format = fmt;
        tcp.num_mips = 1;
        tcp.num_arrays = 1;
        tcp.sample_count = 1;
        tcp.sample_quality = 0;
        tcp.usage = PEN_USAGE_DEFAULT;
        tcp.bind_flags = PEN_BIND_SHADER_RESOURCE;
        tcp.cpu_access_flags = 0;
        tcp.flags = 0;
        tcp.block_size = sizeof(T) * 4;
        tcp.pixels_per_block = 1;
        tcp.data = pen::memory_alloc(data_size);
        tcp.data_size = data_size;

        T* fdata = (T*)tcp.data;

        for (u32 y = 0; y < h; ++y)
        {
            for (u32 x = 0; x < w; ++x)
            {
                u32 i = y * w * 4 + x * 4;

                fdata[i + 0] = r;
                fdata[i + 1] = g;
                fdata[i + 2] = b;
                fdata[i + 3] = a;
            }
        }

        return pen::renderer_create_texture(tcp);
    }

    void load_textures()
    {
        const pen::renderer_info& ri = pen::renderer_get_info();

        bool bc[7];
        bc[0] = ri.caps & PEN_CAPS_TEX_FORMAT_BC1;
        bc[1] = ri.caps & PEN_CAPS_TEX_FORMAT_BC2;
        bc[2] = ri.caps & PEN_CAPS_TEX_FORMAT_BC3;
        bc[3] = ri.caps & PEN_CAPS_TEX_FORMAT_BC4;
        bc[4] = ri.caps & PEN_CAPS_TEX_FORMAT_BC5;
        bc[5] = ri.caps & PEN_CAPS_TEX_FORMAT_BC6;
        bc[6] = ri.caps & PEN_CAPS_TEX_FORMAT_BC7;

        f16 h0 = float_to_half(0.0f);
        f16 h1 = float_to_half(1.0f);

        static typed_texture textures[] = {
            {"rgb8", put::load_texture("data/textures/formats/texfmt_rgb8.dds")},
            {"rgba8", put::load_texture("data/textures/formats/texfmt_rgba8.dds")},

            {"bc1", bc[0] ? put::load_texture("data/textures/formats/texfmt_bc1.dds") : 0},
            {"bc2", bc[1] ? put::load_texture("data/textures/formats/texfmt_bc2.dds") : 0},
            {"bc3", bc[2] ? put::load_texture("data/textures/formats/texfmt_bc3.dds") : 0},

            {"bc4", bc[3] ? put::load_texture("data/textures/formats/texfmt_bc4.dds") : 0},
            {"bc5", bc[4] ? put::load_texture("data/textures/formats/texfmt_bc5.dds") : 0},

            // incorrect in d3d and visual studio (nvcompress issue?)
            {"bc6", bc[5] ? put::load_texture("data/textures/formats/texfmt_bc6.dds") : 0},
            {"bc7", bc[6] ? put::load_texture("data/textures/formats/texfmt_bc7.dds") : 0},

            {"bc1n", bc[0] ? put::load_texture("data/textures/formats/texfmt_bc1n.dds") : 0},
            {"bc3n", bc[2] ? put::load_texture("data/textures/formats/texfmt_bc3n.dds") : 0},

            {"r32f", create_float_texture<f32>(64, 64, PEN_TEX_FORMAT_R32G32B32A32_FLOAT, 1.0f, 0.0f, 1.0f, 1.0f)},
            {"r16f", create_float_texture<f16>(64, 64, PEN_TEX_FORMAT_R16G16B16A16_FLOAT, h0, h1, h0, h1)}};

        s_textures = &textures[0];
        s_num_textures = PEN_ARRAY_SIZE(textures);

        s_texture_formats = new const c8*[s_num_textures];
        for (int i = 0; i < s_num_textures; ++i)
            s_texture_formats[i] = s_textures[i].fromat;
    }

    void texture_formats_ui()
    {
        bool opened = true;
        ImGui::Begin("Texture Formats", &opened, ImGuiWindowFlags_AlwaysAutoResize);

        static s32 current_type = 0;
        ImGui::Combo("Format", &current_type, s_texture_formats, s_num_textures);

        if (s_textures[current_type].handle)
        {
            texture_info info;
            get_texture_info(s_textures[current_type].handle, info);
            ImGui::Image(IMG(s_textures[current_type].handle), ImVec2(info.width, info.height));

            ImVec2 mip_size = ImVec2(info.width, info.height);
            for (u32 i = 0; i < info.num_mips; ++i)
            {
                mip_size.x *= 0.5f;
                mip_size.y *= 0.5f;

                mip_size.x = std::max<f32>(mip_size.x, 1.0f);
                mip_size.y = std::max<f32>(mip_size.y, 1.0f);

                ImGui::SameLine();
                ImGui::Image(IMG(s_textures[current_type].handle), mip_size);
            }
        }
        else
        {
            ImGui::Text("Unsupported on this platform");
        }

        ImGui::End();
    }

    void* user_setup(void* params)
    {
        // unpack the params passed to the thread and signal to the engine it ok to proceed
        s_job_params = (pen::job_thread_params*)params;
        s_thread_info = s_job_params->job_info;
        pen::semaphore_post(s_thread_info->p_sem_continue, 1);

        dev_ui::init();

        // create 2 clear states one for the render target and one for the main screen, so we can see the difference
        pen::clear_state cs = {
            0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0x00, PEN_CLEAR_COLOUR_BUFFER | PEN_CLEAR_DEPTH_BUFFER,
        };

        s_clear_state = pen::renderer_create_clear_state(cs);

        load_textures();

        pen_main_loop(user_update);
        return PEN_THREAD_OK;
    }

    void user_shutdown()
    {
        pen::renderer_new_frame();

        dev_ui::shutdown();

        pen::renderer_release_clear_state(s_clear_state);

        for (u32 i = 0; i < s_num_textures; ++i)
            pen::renderer_release_texture(s_textures[i].handle);

        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();

        pen::semaphore_post(s_thread_info->p_sem_terminated, 1);
    }

    loop_t user_update()
    {
        pen::renderer_new_frame();

        // bind back buffer and clear
        viewport vp = {0.0f, 0.0f, PEN_BACK_BUFFER_RATIO, 1.0f, 0.0f, 1.0f};
        pen::renderer_set_viewport(vp);
        pen::renderer_set_scissor_rect(rect{vp.x, vp.y, vp.width, vp.height});
        pen::renderer_set_targets(PEN_BACK_BUFFER_COLOUR, PEN_BACK_BUFFER_DEPTH);
        pen::renderer_clear(s_clear_state);

        dev_ui::new_frame();

        texture_formats_ui();

        put::dev_ui::render();

        // present
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();

        // msg from the engine we want to terminate
        if (pen::semaphore_try_wait(s_thread_info->p_sem_exit))
        {
            user_shutdown();
            pen_main_loop_exit();
        }

        pen_main_loop_continue();
    }
} // namespace
