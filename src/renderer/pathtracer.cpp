#include "pathtracer.h"
#include "renderer/pathtracer_kernel.h"
#include "renderer/rayqueue.h"

PathTracer::PathTracer(const std::string& filename)
    : m_scene(filename), m_sample_num(0), m_sum_bounce(0), m_reset(false)
{
}

PathTracer::~PathTracer()
{
}

void PathTracer::init()
{
    initScene();
    initQueue();       
}

void PathTracer::initScene()
{
    m_scene.createDeviceData();
}

void PathTracer::initQueue()
{
    const size_t pixels_num = m_scene.m_frame_buffer.size();   
    MemoryArena arena;
    RayQueue input_queue;
    RayQueue scatter_queue;
    RayQueue shadow_queue;

    alloc_queues(pixels_num, input_queue, scatter_queue, shadow_queue, arena);

    m_memory_pool.alloc(arena.size()); 
}

void PathTracer::render(uint32* output)
{
    if (m_reset)
    {
        reset();
    }
    m_reset = false;

    const size_t pixels_num = m_scene.m_frame_buffer.size();

    MemoryArena arena(m_memory_pool.data());
    RayQueue input_queue;
    RayQueue scatter_queue;
    RayQueue shadow_queue;

    alloc_queues(pixels_num, input_queue, scatter_queue, shadow_queue, arena);
    
    SceneView scene_view = m_scene.view();
    FrameBufferView frame_buffer_view = m_scene.m_frame_buffer.view();
    PTContext context;
    context.m_bounce = 0;
    context.m_sample_num = m_sample_num ++;
    context.m_in_queue = input_queue;
    context.m_scatter_queue = scatter_queue;
    context.m_shadow_queue = shadow_queue;
    
    generate_primary_rays(context, scene_view, frame_buffer_view);
    CUDA_CHECK(cudaDeviceSynchronize());
    
    for (context.m_bounce = 0; 
         context.m_bounce < m_options.m_max_path_length; 
         context.m_bounce++)
    {
        //printf("%u\n", context.m_bounce);

        uint32 in_queue_size;
        CUDA_CHECK( cudaMemcpy(&in_queue_size, context.m_in_queue.m_size, sizeof(uint32), cudaMemcpyDeviceToHost) );        
        m_sum_bounce += in_queue_size;
        if (in_queue_size == 0) 
        {
            break;
        }

        // Trace
        {
            trace(scene_view, in_queue_size, context.m_in_queue.m_rays, context.m_in_queue.m_hits);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        // Reset
        cudaMemset(context.m_scatter_queue.m_size, 0, sizeof(uint32));
        cudaMemset(context.m_shadow_queue.m_size, 0, sizeof(uint32));

        // NEE(with MIS) and Sample BSDF
        {
            shade_hit(in_queue_size, context, scene_view, frame_buffer_view);
            CUDA_CHECK(cudaDeviceSynchronize());
        }


        uint32 shadow_queue_size;
        CUDA_CHECK(cudaMemcpy(&shadow_queue_size, context.m_shadow_queue.m_size, sizeof(uint32), cudaMemcpyDeviceToHost));        
        // Solve occlude and Accumulate radiance
        {
            trace_shadow(scene_view, shadow_queue_size, context.m_shadow_queue.m_rays, context.m_shadow_queue.m_hits);
            CUDA_CHECK(cudaDeviceSynchronize());
            accumulate(shadow_queue_size, context, scene_view, frame_buffer_view);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        std::swap(context.m_in_queue, context.m_scatter_queue);
    }

    finish_sample(frame_buffer_view);

    if (output)
    {
        filter(output, frame_buffer_view);        
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    //std::cout << (float)m_sum_bounce / (m_scene.m_frame_buffer.size() * m_sample_num) << std::endl;
}

void PathTracer::render(uint32 num)
{
    init();
    for (int i = 0; i < num; i++)
    {
        fprintf(stderr, "\r%f%%", 100.f * (i + 1) / num);
        render();
    }
    output(std::to_string(m_sample_num + num) + "spp.png");
    exit(0);    
}

void PathTracer::output(const std::string& filename)
{
    m_scene.m_frame_buffer.output(filename);
}

void PathTracer::zoom(float d)
{
    m_scene.m_camera.zoom(d);
    m_reset = true;
}

void PathTracer::translate(float x, float y)
{
    m_scene.m_camera.translate(x, y);
    m_reset = true;
}

void PathTracer::rotate(float yaw, float pitch)
{
    m_scene.m_camera.rotate(yaw, pitch);
    m_reset = true;
}

void PathTracer::reset()
{
    m_sample_num = 0;
    m_sum_bounce = 0;
    m_scene.m_frame_buffer.clear();
}

void PathTracer::resize(uint32 width, uint32 height)
{
    m_scene.m_frame_buffer.resize(width, height);
    m_scene.m_camera.updateAspectRation((float)(width) / height);    
    initQueue();
    m_reset = true;
}

int2 PathTracer::getResolution() const
{
    return m_scene.m_frame_buffer.getResolution();
}