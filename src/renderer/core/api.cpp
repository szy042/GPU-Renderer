#include "api.h"

#include "renderer/core/transform.h"
#include "renderer/core/primitive.h"

#include "renderer/core/scene.h"
#include "renderer/core/integrator.h"
#include "renderer/core/renderer.h"

#include "renderer/core/camera.h"

#include "renderer/core/film.h"

#include "renderer/core/filter.h"

#include "renderer/core/sampler.h"

#include "renderer/core/shape.h"
#include "renderer/shape/trianglemesh.h"

#include "renderer/core/material.h"
#include "renderer/material/matte.h"

#include "renderer/light/arealight.h"


class Options {
public:
    Options() {
        m_currentTransform.Identity();
        m_hasAreaLight = false;
    }

    void 
        MakeNamedMaterial(
        const std::string& name, 
        const ParameterSet& params);

    std::shared_ptr<Material> 
        GetNamedMaterial(
        const std::string& name) const;

    std::shared_ptr<Material>
        MakeMaterial(
            const std::string& type,
            const ParameterSet& params);

    std::vector<std::shared_ptr<Shape>>
        MakeShape(
            const std::string& type,
            const ParameterSet& params);

    std::shared_ptr<AreaLight>
        MakeAreaLight(
            const std::string& type,
            const ParameterSet& params,
            const std::shared_ptr<Shape>& s);

    void MakeCamera();
    void MakeFilm();
    void MakeFilter();
    void MakeSampler();
    void MakeIntegrator();
    void MakeScene();
    std::shared_ptr<Renderer> 
        MakeRenderer();

    Transform m_currentTransform;
    std::vector<Transform> m_transformStack;

    std::string m_integratorType;
    ParameterSet m_integratorParameterSet;
    std::string m_samplerType;
    ParameterSet m_samplerParameterSet;
    std::string m_filterType;
    ParameterSet m_filterParameterSet;
    std::string m_filmType;
    ParameterSet m_filmParameterSet;
    std::string m_cameraType;
    ParameterSet m_cameraParameterSet;
    Transform m_cameraTransform;

    std::shared_ptr<Camera> m_camera;
    std::shared_ptr<Film> m_film;
    std::shared_ptr<Filter> m_filter;
    std::shared_ptr<Sampler> m_sampler;
    std::shared_ptr<Integrator> m_integrator;

    bool m_hasAreaLight;
    std::string m_areaLightType;
    ParameterSet m_areaLightParameterSet;

    std::shared_ptr<Material> m_currentMaterial;
    std::map<std::string, std::shared_ptr<Material>> m_namedMaterials;
    std::shared_ptr<Medium> m_currentMedium;
    std::map<std::string, std::shared_ptr<Medium>> m_namedMedium;

    std::vector<std::shared_ptr<Light>> m_lights;
    std::vector<std::shared_ptr<Primitive>> m_primitives;
    std::shared_ptr<Scene> m_scene;

};

static std::unique_ptr<Options> options(new Options);

void apiAttributeBegin()
{
    options->m_transformStack.push_back(options->m_currentTransform);
}

void apiAttributeEnd()
{
    options->m_currentTransform = options->m_transformStack.back();
    options->m_transformStack.pop_back();
}

std::shared_ptr<Renderer> 
apiWorldEnd()
{
    /*
     * Camera
     *   -- Film
     *        |- Filter
     *        -- Sampler
     */

    options->MakeFilm();
    options->MakeCamera();
    options->MakeIntegrator();
    options->MakeScene();
    return options->MakeRenderer();
}

void apiTransform(const Float m[16])
{
    Transform t(m);
    options->m_currentTransform *= t;
}

void apiIntegrator(const std::string& type, ParameterSet params)
{
    options->m_integratorType = type;
    options->m_integratorParameterSet = params;
}

void apiSampler(const std::string& type, ParameterSet params)
{
    options->m_samplerType = type;
    options->m_samplerParameterSet = params;
}

void apiFilter(const std::string& type, ParameterSet params)
{
    options->m_filterType = type;
    options->m_filterParameterSet = params;
}

void apiFilm(const std::string& type, ParameterSet params) {
    options->m_filmType = type;
    options->m_filmParameterSet = params;
}

void apiCamera(const std::string& type, ParameterSet params)
{
    options->m_cameraType = type;
    options->m_cameraParameterSet = params;
    options->m_cameraTransform = options->m_currentTransform;
}

void apiNamedMaterial(const std::string& name, ParameterSet params)
{
    options->m_currentMaterial = options->GetNamedMaterial(name);
}

void apiMakeNamedMaterial(const std::string& name, ParameterSet params)
{
    options->MakeNamedMaterial(name, params);
}

void apiShape(const std::string& type, ParameterSet params)
{ 
    std::vector<std::shared_ptr<Shape>> shapes = options->MakeShape(
        type, params);
    std::shared_ptr<Material> mtl = options->m_currentMaterial;
    for (auto s : shapes) {
        std::shared_ptr<AreaLight> areaLight;
        if (options->m_hasAreaLight) {
            areaLight = options->MakeAreaLight(
                options->m_areaLightType, options->m_areaLightParameterSet, s);
            options->m_lights.push_back(areaLight);
        }
        options->m_primitives.push_back(std::make_shared<Primitive>(s, mtl, areaLight));
    }
}

void apiAreaLightSource(const std::string& type, ParameterSet params)
{
    options->m_areaLightType = type;
    options->m_areaLightParameterSet = params;
}

void 
Options::MakeNamedMaterial(
    const std::string& name, 
    const ParameterSet& params)
{
    std::string type = params.GetString("type");
    std::shared_ptr<Material> mtl = MakeMaterial(type, params);
    m_namedMaterials[name] = mtl;
}

std::shared_ptr<Material> 
Options::GetNamedMaterial(
    const std::string& name) const
{
    ASSERT(m_namedMaterials.count(name), "No named material " + name);
    return m_namedMaterials.find(name)->second;
}

std::shared_ptr<Material> 
Options::MakeMaterial(
    const std::string& type, 
    const ParameterSet& params)
{
    Material* mtl = nullptr;
    if (type == "Diffuse" || type == "matte") {
        mtl = CreateMatteMaterial(params);
    }
    else {
        ASSERT(0, "Can't support material " + type);
    }
    return std::shared_ptr<Material>(mtl);
}

std::vector<std::shared_ptr<Shape>> 
Options::MakeShape(
    const std::string& type, 
    const ParameterSet& params)
{
    Transform objToWorld = m_currentTransform;
    Transform worldToObj = Inverse(objToWorld);
    std::vector<std::shared_ptr<Shape>> shapes;
    if (type == "trianglemesh") {
        shapes = CreateTriangleMeshShape(params, objToWorld, worldToObj);
    }
    else {
        ASSERT(0, "Can't support shape " + type);
    }
    return shapes;
}

std::shared_ptr<AreaLight> 
Options::MakeAreaLight(
    const std::string& type, 
    const ParameterSet& params, 
    const std::shared_ptr<Shape>& s)
{
    std::shared_ptr<AreaLight> areaLight;
    if (type == "area" || type == "diffuse"){
        areaLight = CreateAreaLight(params, s);
    }
    return areaLight;
}

void Options::MakeCamera()
{
    Transform objToWorld = m_cameraTransform;
    Transform worldToObj = Inverse(objToWorld);
    m_camera = CreateCamera(m_cameraParameterSet, objToWorld, worldToObj);
}

void Options::MakeFilm(){
    m_film = CreateFilm(m_filmParameterSet);
}

void Options::MakeFilter()
{
}

void Options::MakeSampler()
{
}

void Options::MakeIntegrator()
{
    m_integrator = std::make_shared<Integrator>();    
}

void Options::MakeScene()
{
    m_scene = std::make_shared<Scene>();
    m_scene->m_lights = std::move(m_lights);
    m_scene->m_primitives = std::move(m_primitives);
}

std::shared_ptr<Renderer> 
Options::MakeRenderer()
{
    auto r = std::make_shared<Renderer>();
    r->m_scene = m_scene;
    r->m_camera = m_camera;
    r->m_integrator = m_integrator;
    return r;
}
