#pragma once
#ifndef __CPURENDERER_H
#define __CPURENDERER_H

#include "renderer/core/renderer.h"
#include "renderer/core/film.h"
#include "renderer/core/sampling.h"

inline Point3f 
WorldToRaster(Camera* camera, Point3f p) {
    Float znear = 1e-2;
    Point3f pHit1 = p;
    Point3f pCamera1 = camera->m_worldToCamera(pHit1);
    Point3f pCameraFilm1(pCamera1.x / pCamera1.z * znear, pCamera1.y / pCamera1.z * znear, znear);
    Point3f pFilm1 = camera->m_cameraToRaster(pCameraFilm1);
    return pFilm1;
}

void DrawTransportLine(Point2i p, Renderer& renderer);

inline
Float PowerHeuristic(int nf, Float fPdf, int ng, Float gPdf) {
    Float f = nf * fPdf, g = ng * gPdf;
    return (f * f) / (f * f + g * g);
}

inline
Spectrum NextEventEstimate(const Scene& scene, const Interaction& inter, unsigned int& seed, Point3f& pLight)
{
    const Primitive& primitive = scene.m_primitives[inter.m_primitiveID];
    const Material& material = scene.m_materials[primitive.m_materialID];

    Spectrum est;

    // Sample one of lights
    int lightID = min(scene.m_lights.size() - 1, int(NextRandom(seed) * scene.m_lights.size()));
    Float lightChoosePdf = Float(1) / scene.m_lights.size();
    const Light& light = scene.m_lights[lightID];

    // Light Sampling
    {
        // Light Sample Li
        const Triangle& triangle = scene.m_triangles[light.m_shapeID];
        Float lightSamplePdf;
        Interaction lightSample = triangle.Sample(&lightSamplePdf, seed);
        pLight = lightSample.m_p;
        lightSamplePdf *= (lightSample.m_p - inter.m_p).SqrLength() /
            AbsDot(-Normalize(lightSample.m_p - inter.m_p), lightSample.m_shadingN);

        // Visibility test
        Point3f origin = inter.m_p + Normalize(lightSample.m_p - inter.m_p) * Epsilon;
        Point3f target = lightSample.m_p + Normalize(origin - lightSample.m_p) * Epsilon;
        Vector3f d = target - origin;
        Ray testRay(origin, Normalize(d), d.Length() - Epsilon);
        bool hit = scene.Intersect(testRay);

        if (!hit) {
            Vector3f d = Normalize(lightSample.m_p - inter.m_p);
            // Get Le
            Spectrum Le(0.);
            if (Dot(-d, lightSample.m_shadingN) > 0) {
                Le = light.m_L;
            }

            // BSDF Sample
            Normal3f n = Faceforward(inter.m_shadingN, inter.m_wo);
            Float bsdfPdf;
            Spectrum cosBSDF;
            cosBSDF = material.F(n, inter.m_wo, d, &bsdfPdf);

            // Contribution
            if (light.isDelta()) {
                est += Le * cosBSDF / lightSamplePdf;
            }
            else {
                Float weight = PowerHeuristic(1, lightSamplePdf, 1, bsdfPdf);
                est += Le * cosBSDF * weight / lightSamplePdf;
            }
        }
    }

    // BSDF Sampling
    if (!light.isDelta()) {

        // BSDF Sample
        Normal3f n = Faceforward(inter.m_shadingN, inter.m_wo);
        Float bsdfPdf;
        Spectrum cosBSDF;
        Vector3f wi;
        cosBSDF = material.Sample(n, inter.m_wo, &wi, &bsdfPdf, seed);

        // Light Sample
        const Triangle& triangle = scene.m_triangles[light.m_shapeID];

        Point3f origin = inter.m_p + wi * Epsilon;
        Ray testRay(origin, wi);
        Interaction lightInter;
        bool hit = scene.IntersectP(testRay, &lightInter);

        if (hit && scene.m_primitives[lightInter.m_primitiveID].m_lightID == lightID) {
            Float lightSamplePdf;
            lightSamplePdf = (lightInter.m_p - inter.m_p).SqrLength() /
                (AbsDot(-wi, lightInter.m_shadingN) * triangle.Area());            
            pLight = lightInter.m_p;

            // Get Le            
            Spectrum Le(0.);
            if (Dot(-wi, lightInter.m_shadingN) > 0) {
                Le = light.m_L;
            }

            Float weight = PowerHeuristic(1, bsdfPdf, 1, lightSamplePdf);
            //printf("%f\n", bsdfPdf);
            est += Le * cosBSDF * weight / bsdfPdf;
            //return est / lightChoosePdf;
        }
    }

    return est / lightChoosePdf;
}

Spectrum SampleMaterial(const Scene& scene, Interaction& inter, unsigned int& seed) {
    const Primitive& primitive = scene.m_primitives[inter.m_primitiveID];
    const Material& material = scene.m_materials[primitive.m_materialID];

    Normal3f n = Faceforward(inter.m_shadingN, inter.m_wo);

    Float bsdfPdf;
    Spectrum cosBSDF = material.Sample(n, inter.m_wo, &inter.m_wi, &bsdfPdf, seed);

    return cosBSDF / bsdfPdf;
}

inline 
void render(std::shared_ptr<Renderer> renderer)
{
    Integrator* integrator = &renderer->m_integrator;
    Camera* camera = &renderer->m_camera;
    Scene* scene = &renderer->m_scene;    

    int num = integrator->m_nSample;
    for (int x = 0; x < camera->m_film.m_resolution.x; x++) {
        for (int y = 0; y < camera->m_film.m_resolution.y; y++) {
            int index = y * camera->m_film.m_resolution.x + x;
            unsigned int seed = InitRandom(index, 0);
            for (int k = 0; k < num; k++) {                
                Spectrum L(0);
                Spectrum throughput(1);
                Ray ray = camera->GenerateRay(Point2f(x + NextRandom(seed), y + NextRandom(seed)));
                for (int i = 0; i < integrator->m_maxDepth; i++) {

                    // find intersection with scene
                    Interaction interaction;
                    bool hit = scene->IntersectP(ray, &interaction);

                    if (!hit) {
                        break;
                    }

                    const Primitive& primitive = scene->m_primitives[interaction.m_primitiveID];
                    if (i == 0 && primitive.m_lightID != -1) {
                        int lightID = primitive.m_lightID;
                        const Light& light = scene->m_lights[lightID];
                        if (Dot(interaction.m_shadingN, interaction.m_wo) > 0) {
                            L += throughput * light.m_L;
                        }
                    }

                    // render normal
                    //L = Spectrum(interaction.m_geometryN);
                    //break;

                    // get material's bsdf
                    //const Material& material = scene->m_materials[primitive.m_materialID];

                    // direct light
                    Point3f pLight;
                    L += throughput * NextEventEstimate(*scene, interaction, seed, pLight);


                    // calculate BSDF
                    throughput *= SampleMaterial(*scene, interaction, seed);

                    // delete BSDF
                    delete interaction.m_bsdf;

                    // indirect light                    
                    if (throughput.Max() < 1 && i > 3) {
                        Float q = max((Float).05, 1 - throughput.Max());
                        if (NextRandom(seed) < q) break;
                        throughput /= 1 - q;
                    }

                    ray.o = interaction.m_p + interaction.m_wi * Epsilon;
                    ray.d = interaction.m_wi;
                    ray.tMax = Infinity;
                }
                camera->m_film.AddSample(x, y, L);
            }
        }
    }

    //DrawTransportLine(Point2i(234, 234), *renderer);

    camera->m_film.Output();
}

void DrawTransportLine(Point2i p, Renderer& renderer) {
    Integrator* integrator = &renderer.m_integrator;
    Camera* camera = &renderer.m_camera;
    Film* film = &camera->m_film;
    Scene* scene = &renderer.m_scene;
    
    int index = p.y * camera->m_film.m_resolution.x + p.x;
    unsigned int seed = InitRandom(index, 0);    
    std::vector<Point3f> vertex;

    Spectrum L(0);
    Spectrum throughput(1);
    Ray ray = camera->GenerateRay(Point2f(p.x + NextRandom(seed), p.y + NextRandom(seed)));
    for (int i = 0; i < integrator->m_maxDepth; i++) {

        // find intersection with scene
        Interaction interaction;
        bool hit = scene->IntersectP(ray, &interaction);

        if (!hit) {
            break;
        }

        vertex.push_back(interaction.m_p);

        if (Dot(ray.d, interaction.m_geometryN) > 0)
        {
            interaction.m_geometryN = -interaction.m_geometryN;
        }
        if (Dot(ray.d, interaction.m_shadingN) > 0) {
            interaction.m_shadingN = -interaction.m_shadingN;
        }

        const Primitive& primitive = scene->m_primitives[interaction.m_primitiveID];
        if (i == 0 && primitive.m_lightID != -1) {
            int lightID = primitive.m_lightID;
            const Light& light = scene->m_lights[lightID];
            if (Dot(interaction.m_shadingN, interaction.m_wo) > 0) {
                L += throughput * light.m_L;
            }
        }

        // direct light
        Point3f pLight;
        Spectrum neeVal = NextEventEstimate(*scene, interaction, seed, pLight);
        L += throughput * neeVal;
        if (!neeVal.isBlack()) {
            film->DrawLine(Point2f(WorldToRaster(camera, interaction.m_p)), Point2f(WorldToRaster(camera, pLight)), Spectrum(1, 1, 0));
        }

        // calculate BSDF
        throughput *= SampleMaterial(*scene, interaction, seed);

        if (throughput.Max() < 1 && i > 3) {
            Float q = max((Float).05, 1 - throughput.Max());
            if (NextRandom(seed) < q) break;
            throughput /= 1 - q;
        }

        ray.o = interaction.m_p + interaction.m_wi * Epsilon;
        ray.d = interaction.m_wi;
        ray.tMax = Infinity;
    }
    for (int i = 1; i < vertex.size(); i++) {
        film->DrawLine(Point2f(WorldToRaster(camera, vertex[i-1])), Point2f(WorldToRaster(camera, vertex[i])), Spectrum(1, 1, 1));
    }
}

#endif // !__CPURENDERER_H
