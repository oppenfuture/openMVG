// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2016 cDc <cdc.seacave@gmail.com>, Pierre MOULON

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/cameras/Camera_Pinhole.hpp"
#include "openMVG/cameras/Camera_undistort_image.hpp"
#include "openMVG/image/image_io.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/sfm/sfm_data_io.hpp"

#define _USE_EIGEN
#include "InterfaceMVS.h"

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "third_party/progress/progress_display.hpp"

using namespace openMVG;
using namespace openMVG::cameras;
using namespace openMVG::geometry;
using namespace openMVG::image;
using namespace openMVG::sfm;

#include <cstdlib>
#include <string>

template<typename Image, typename RemapType>
void UndistortImage(
    const Image& imageIn,
    Image& image_ud,
    const RemapType& map_row,
    const RemapType& map_col,
    typename Image::Tpixel fillcolor = typename Image::Tpixel(0))
{
    int width = imageIn.Width();
    int height = imageIn.Height();

    image_ud.resize(width, height, true, fillcolor);

    const image::Sampler2d<image::SamplerLinear> sampler;

#ifdef OPENMVG_USE_OPENMP
    #pragma omp parallel for
#endif
    for (int j = 0; j < height; ++j)
        for (int i = 0; i < width; ++i)
        {
            // compute coordinates with distortion
            double row = map_row(j, i);
            double col = map_col(j, i);
            // pick pixel if it is in the image domain
            if (imageIn.Contains(row, col))
            {
                image_ud(j, i) = sampler(imageIn, row, col);
            }
        }
}

template<typename RemapType>
void CalcCameraDistortionMap(
    const openMVG::cameras::IntrinsicBase* cam,
    int width,
    int height,
    RemapType& out_map_row,
    RemapType& out_map_col
)
{
    out_map_row.resize(width, height);
    out_map_col.resize(width, height);

#ifdef OPENMVG_USE_OPENMP
    #pragma omp parallel for
#endif
    for (int j = 0; j < height; j++)
        for (int i = 0; i < width; i++)
        {
            const Vec2 undisto_pix(i, j);
            // compute coordinates with distortion
            const Vec2 disto_pix = cam->get_d_pixel(undisto_pix);

            out_map_row(j, i) = disto_pix[1];
            out_map_col(j, i) = disto_pix[0];
        }
}

bool exportToUndistortImages(
    const SfM_Data& sfm_data,
    const std::string& sOutDir
)
{
    if (!stlplus::is_folder(sOutDir))
    {
        stlplus::folder_create(sOutDir);
        if (!stlplus::is_folder(sOutDir))
        {
            std::cerr << "Cannot access to one of the desired output directory" << std::endl;
            return false;
        }
    }

    if (0 == sfm_data.GetViews().size())
    {
        std::cout << "No view found in sfm data" << std::endl;
        return false;
    }

    auto& view_iter = sfm_data.GetViews().begin();
    while (!sfm_data.IsPoseAndIntrinsicDefined(view_iter->second.get()) && (view_iter != sfm_data.GetViews().end())) {
        view_iter++;
    }

    if (sfm_data.GetViews().end() == view_iter) {
        std::cout << "No view has pose and intrinsic defined" << std::endl;
        return false;
    }

    // all views should share the same intrinsic
    const openMVG::cameras::IntrinsicBase* cam_picked = sfm_data.GetIntrinsics().at(view_iter->second->id_intrinsic).get();

    const std::string srcImage_picked = stlplus::create_filespec(sfm_data.s_root_path, view_iter->second->s_Img_path);

    if (!stlplus::is_file(srcImage_picked))
    {
        std::cout << "Cannot read the corresponding image: " << srcImage_picked << std::endl;
        return false;
    }

    Image<openMVG::image::RGBColor> imageRGB_picked;
    ReadImage(srcImage_picked.c_str(), &imageRGB_picked);

    int img_width = imageRGB_picked.Width();
    int img_height = imageRGB_picked.Height();

    Image<double> map_row;
    Image<double> map_col;

    if (cam_picked->have_disto())
    {
        CalcCameraDistortionMap(cam_picked, img_width, img_height, map_row, map_col);
    }


//#ifdef OPENMVG_USE_OPENMP
//    #pragma omp parallel for
//#endif
    for (const auto& view : sfm_data.GetViews())
    {
        const std::string srcImage = stlplus::create_filespec(sfm_data.s_root_path, view.second->s_Img_path);

        const std::string outputImage = stlplus::create_filespec(sOutDir, view.second->s_Img_path);

        if (!stlplus::is_file(srcImage))
        {
            std::cout << "Cannot read the corresponding image: " << srcImage << std::endl;
            return false;
        }
        if (sfm_data.IsPoseAndIntrinsicDefined(view.second.get()))
        {
            // export undistorted images
            const openMVG::cameras::IntrinsicBase* cam = sfm_data.GetIntrinsics().at(view.second->id_intrinsic).get();

            // assure this camera has the same intrinsic as the picked one. Is it adequate??
            assert((cam_picked->getType() == cam->getType()) && "A camera with different intrinsic found");
            assert((cam_picked->have_disto() == cam->have_disto()) && "A camera with different intrinsic found");
            assert((cam_picked->w() == cam->w()) && (cam_picked->h() == cam->h()) && ("A camera with different intrinsic found") );
            
            if (cam_picked->have_disto())
            {
                // undistort image and save it
                Image<openMVG::image::RGBColor> imageRGB, imageRGB_ud;
                ReadImage(srcImage.c_str(), &imageRGB);
                // UndistortImage(imageRGB, cam, imageRGB_ud, BLACK);
                UndistortImage(imageRGB, imageRGB_ud, map_row, map_col, BLACK);
                WriteImage(outputImage.c_str(), imageRGB_ud);
                // undistort mask and save it
                auto maskFileName = srcImage + ".mask.png";
                if (stlplus::is_file(maskFileName)) {
                    Image<openMVG::image::RGBColor> maskRGB, maskRGB_ud;
                    ReadImage(maskFileName.c_str(), &maskRGB);
                    //UndistortImage(maskRGB, cam, maskRGB_ud, BLACK);
                    UndistortImage(maskRGB, maskRGB_ud, map_row, map_col, BLACK);
                    WriteImage((outputImage + ".mask.png").c_str(), maskRGB_ud);
                }
                // undistort depth and save it
                auto depthFileBasename = view.second->s_Img_path;
                auto pos = depthFileBasename.find("rgb");
                if (pos != std::string::npos) {
                    depthFileBasename.replace(pos, 3, "depth");
                }
                pos = depthFileBasename.find(".png");
                if (pos != std::string::npos) {
                    depthFileBasename.replace(pos, 4, ".pfm");
                }
                auto depthFileName = stlplus::create_filespec(sfm_data.s_root_path, depthFileBasename);

                if (depthFileBasename != view.second->s_Img_path && stlplus::is_file(depthFileName)) {
                    Image<float> depth_float, depth_float_ud;
                    ReadImage(depthFileName.c_str(), &depth_float);
                    // UndistortImage(depth_float, cam, depth_float_ud, 0.f);
                    UndistortImage(depth_float, depth_float_ud, map_row, map_col, 0.0f);
                    WriteImage((outputImage + ".ref.pfm").c_str(), depth_float_ud);
                }
            }
            else
            {
                // just copy image
                stlplus::file_copy(srcImage, outputImage);
                stlplus::file_copy(srcImage + ".mask.png", outputImage + ".mask.png");
                stlplus::file_copy(srcImage + ".ref.pfm", outputImage + ".ref.pfm");
            }
        }
        else
        {
            // just copy the image
            stlplus::file_copy(srcImage, outputImage);
        }
    }
    return true;
}

bool exportToOpenMVS(
    const SfM_Data& sfm_data,
    const std::string& sOutFile
)
{
    // all images should share the same fScale
    float fScale = -1.0;

    // Export data :
    MVS::Interface scene;
    size_t nPoses(0);
    const uint32_t nViews((uint32_t)sfm_data.GetViews().size());

    C_Progress_display my_progress_bar(nViews);

    // OpenMVG can have not contiguous index, use a map to create the required OpenMVS contiguous ID index
    std::map<openMVG::IndexT, uint32_t> map_intrinsic, map_view;

    // define a platform with all the intrinsic group
    for (const auto& intrinsic : sfm_data.GetIntrinsics())
    {
        if (isPinhole(intrinsic.second->getType()))
        {
            const Pinhole_Intrinsic* cam = dynamic_cast<const Pinhole_Intrinsic*>(intrinsic.second.get());
            if (map_intrinsic.count(intrinsic.first) == 0)
                map_intrinsic.insert(std::make_pair(intrinsic.first, scene.platforms.size()));
            MVS::Interface::Platform platform;
            // add the camera
            MVS::Interface::Platform::Camera camera;
            camera.K = cam->K();
            // sub-pose
            camera.R = Mat3::Identity();
            camera.C = Vec3::Zero();
            platform.cameras.push_back(camera);
            scene.platforms.push_back(platform);
        }
    }

    // define images & poses
    scene.images.reserve(nViews);
    for (const auto& view : sfm_data.GetViews())
    {
        map_view[view.first] = scene.images.size();
        MVS::Interface::Image image;
        // store relative path because we going to feed mvs resized images
        image.name = view.second->s_Img_path;
        /*const std::string srcImage = stlplus::create_filespec(sfm_data.s_root_path, view.second->s_Img_path);*/
        //const std::string outputImage = stlplus::create_filespec(sOutDir, view.second->s_Img_path);
        image.platformID = map_intrinsic.at(view.second->id_intrinsic);
        MVS::Interface::Platform& platform = scene.platforms[image.platformID];
        image.cameraID = 0;


        if (sfm_data.IsPoseAndIntrinsicDefined(view.second.get()))
        {
            MVS::Interface::Platform::Pose pose;
            image.poseID = platform.poses.size();
            const openMVG::geometry::Pose3 poseMVG(sfm_data.GetPoseOrDie(view.second.get()));
            pose.R = poseMVG.rotation();
            pose.C = poseMVG.center();

            // assigned only once
            if (fScale < 0.0)
            {
                const std::string srcImage = stlplus::create_filespec(sfm_data.s_root_path, view.second->s_Img_path);

                if (stlplus::is_file(srcImage)) {
                    // read image meta-data
                    ImageHeader imageHeader;
                    ReadImageHeader(srcImage.c_str(), &imageHeader);
                    fScale = (1.0 / std::max(imageHeader.width, imageHeader.height));
                }
            }

            //// export undistorted images
            //const openMVG::cameras::IntrinsicBase* cam = sfm_data.GetIntrinsics().at(view.second->id_intrinsic).get();
            //if (cam->have_disto())
            //{
            //    // undistort image and save it
            //    Image<openMVG::image::RGBColor> imageRGB, imageRGB_ud;
            //    ReadImage(srcImage.c_str(), &imageRGB);
            //    UndistortImage(imageRGB, cam, imageRGB_ud, BLACK);
            //    WriteImage(outputImage.c_str(), imageRGB_ud);
            //    // undistort mask and save it
            //    auto maskFileName = srcImage + ".mask.png";
            //    if (stlplus::is_file(maskFileName)) {
            //        Image<openMVG::image::RGBColor> maskRGB, maskRGB_ud;
            //        ReadImage(maskFileName.c_str(), &maskRGB);
            //        UndistortImage(maskRGB, cam, maskRGB_ud, BLACK);
            //        WriteImage((outputImage + ".mask.png").c_str(), maskRGB_ud);
            //    }
            //    // undistort depth and save it
            //    auto depthFileBasename = view.second->s_Img_path;
            //    auto pos = depthFileBasename.find("rgb");
            //    if (pos != std::string::npos) {
            //        depthFileBasename.replace(pos, 3, "depth");
            //    }
            //    pos = depthFileBasename.find(".png");
            //    if (pos != std::string::npos) {
            //        depthFileBasename.replace(pos, 4, ".pfm");
            //    }
            //    auto depthFileName = stlplus::create_filespec(sfm_data.s_root_path, depthFileBasename);
            //    if (depthFileBasename != view.second->s_Img_path && stlplus::is_file(depthFileName)) {
            //        Image<float> depth_float, depth_float_ud;
            //        ReadImage(depthFileName.c_str(), &depth_float);
            //        UndistortImage(depth_float, cam, depth_float_ud, 0.f);
            //        WriteImage((outputImage + ".ref.pfm").c_str(), depth_float_ud);
            //    }
            //}
            //else
            //{
            //    // just copy image
            //    stlplus::file_copy(srcImage, outputImage);
            //    stlplus::file_copy(srcImage + ".mask.png", outputImage + ".mask.png");
            //    stlplus::file_copy(srcImage + ".ref.pfm", outputImage + ".ref.pfm");
            //}
            platform.poses.push_back(pose);
            ++nPoses;
        }
        else
        {
            // image have not valid pose, so set an undefined pose
            image.poseID = NO_ID;
            // just copy the image
            // stlplus::file_copy(srcImage, outputImage);
        }
        scene.images.emplace_back(image);
        ++my_progress_bar;
    }

    if (fScale < 0.0)
    {
        std::cout << "No image file found" << std::endl;
        return false;
    }

    // define structure
    scene.vertices.reserve(sfm_data.GetLandmarks().size());
    for (const auto& vertex : sfm_data.GetLandmarks())
    {
        const Landmark& landmark = vertex.second;
        MVS::Interface::Vertex vert;
        MVS::Interface::Vertex::ViewArr& views = vert.views;
        for (const auto& observation : landmark.obs)
        {
            const auto it(map_view.find(observation.first));
            if (it != map_view.end()) {
                MVS::Interface::Vertex::View view;
                view.imageID = it->second;
                view.confidence = 0;
                views.push_back(view);
            }
        }
        if (views.size() < 2)
            continue;
        std::sort(
            views.begin(), views.end(),
            [](const MVS::Interface::Vertex::View & view0, const MVS::Interface::Vertex::View & view1)
            {
                return view0.imageID < view1.imageID;
            }
        );
        vert.X = landmark.X.cast<float>();
        scene.vertices.push_back(vert);
    }

    // normalize camera intrinsics
    for (size_t p = 0; p < scene.platforms.size(); ++p)
    {
        MVS::Interface::Platform& platform = scene.platforms[p];
        for (size_t c = 0; c < platform.cameras.size(); ++c) {
            MVS::Interface::Platform::Camera& camera = platform.cameras[c];
            // find one image using this camera
            MVS::Interface::Image* pImage(nullptr);
            for (MVS::Interface::Image& image : scene.images)
            {
                if (image.platformID == p && image.cameraID == c && image.poseID != NO_ID)
                {
                    pImage = &image;
                    break;
                }
            }
            if (pImage == nullptr)
            {
                std::cerr << "error: no image using camera " << c << " of platform " << p << std::endl;
                continue;
            }
            // read image meta-data
            /*ImageHeader imageHeader;
            ReadImageHeader(stlplus::create_filespec(sOutDir, pImage->name).c_str(), &imageHeader);
            const double fScale(1.0 / std::max(imageHeader.width, imageHeader.height));*/
            camera.K(0, 0) *= fScale;
            camera.K(1, 1) *= fScale;
            camera.K(0, 2) *= fScale;
            camera.K(1, 2) *= fScale;
        }
    }

    // write OpenMVS data
    if (!MVS::ARCHIVE::SerializeSave(scene, sOutFile))
        return false;

    std::cout
        << "Scene saved to OpenMVS interface format:\n"
        << " #platforms: " << scene.platforms.size() << std::endl;
    for (int i = 0; i < scene.platforms.size(); ++i)
    {
        std::cout << "  platform ( " << i << " ) #cameras: " << scene.platforms[i].cameras.size() << std::endl;
    }
    std::cout
        << "  " << scene.images.size() << " images (" << nPoses << " calibrated)\n"
        << "  " << scene.vertices.size() << " Landmarks\n";
    return true;
}

bool exportToUndistortImagesAndOpenMVS(
    const SfM_Data& sfm_data,
    const std::string& sOutFile,
    const std::string& sOutDir
)
{
    if (!exportToUndistortImages(sfm_data, sOutDir))
    {
        return false;
    }

    return exportToOpenMVS(sfm_data, sOutFile);
}

//bool exportToOpenMVS(
//  const SfM_Data & sfm_data,
//  const std::string & sOutFile,
//  const std::string & sOutDir
//  )
//{
//  // Create undistorted images directory structure
//  if (!stlplus::is_folder(sOutDir))
//  {
//    stlplus::folder_create(sOutDir);
//    if (!stlplus::is_folder(sOutDir))
//    {
//      std::cerr << "Cannot access to one of the desired output directory" << std::endl;
//      return false;
//    }
//  }
//
//  // Export data :
//  MVS::Interface scene;
//  size_t nPoses(0);
//  const uint32_t nViews((uint32_t)sfm_data.GetViews().size());
//
//  C_Progress_display my_progress_bar(nViews);
//
//  // OpenMVG can have not contiguous index, use a map to create the required OpenMVS contiguous ID index
//  std::map<openMVG::IndexT, uint32_t> map_intrinsic, map_view;
//
//  // define a platform with all the intrinsic group
//  for (const auto& intrinsic: sfm_data.GetIntrinsics())
//  {
//    if (isPinhole(intrinsic.second->getType()))
//    {
//      const Pinhole_Intrinsic * cam = dynamic_cast<const Pinhole_Intrinsic*>(intrinsic.second.get());
//      if (map_intrinsic.count(intrinsic.first) == 0)
//        map_intrinsic.insert(std::make_pair(intrinsic.first, scene.platforms.size()));
//      MVS::Interface::Platform platform;
//      // add the camera
//      MVS::Interface::Platform::Camera camera;
//      camera.K = cam->K();
//      // sub-pose
//      camera.R = Mat3::Identity();
//      camera.C = Vec3::Zero();
//      platform.cameras.push_back(camera);
//      scene.platforms.push_back(platform);
//    }
//  }
//
//  // define images & poses
//  scene.images.reserve(nViews);
//  for (const auto& view : sfm_data.GetViews())
//  {
//    map_view[view.first] = scene.images.size();
//    MVS::Interface::Image image;
//    // store relative path because we going to feed mvs resized images
//    image.name = view.second->s_Img_path;
//    const std::string srcImage = stlplus::create_filespec(sfm_data.s_root_path, view.second->s_Img_path);
//    const std::string outputImage = stlplus::create_filespec(sOutDir, view.second->s_Img_path);
//    image.platformID = map_intrinsic.at(view.second->id_intrinsic);
//    MVS::Interface::Platform& platform = scene.platforms[image.platformID];
//    image.cameraID = 0;
//    if (!stlplus::is_file(srcImage))
//    {
//      std::cout << "Cannot read the corresponding image: " << srcImage << std::endl;
//      return EXIT_FAILURE;
//    }
//    if (sfm_data.IsPoseAndIntrinsicDefined(view.second.get()) && stlplus::is_file(srcImage))
//    {
//      MVS::Interface::Platform::Pose pose;
//      image.poseID = platform.poses.size();
//      const openMVG::geometry::Pose3 poseMVG(sfm_data.GetPoseOrDie(view.second.get()));
//      pose.R = poseMVG.rotation();
//      pose.C = poseMVG.center();
//      // export undistorted images
//      const openMVG::cameras::IntrinsicBase * cam = sfm_data.GetIntrinsics().at(view.second->id_intrinsic).get();
//      if (cam->have_disto())
//      {
//        // undistort image and save it
//        Image<openMVG::image::RGBColor> imageRGB, imageRGB_ud;
//        ReadImage(srcImage.c_str(), &imageRGB);
//        UndistortImage(imageRGB, cam, imageRGB_ud, BLACK);
//        WriteImage(outputImage.c_str(), imageRGB_ud);
//        // undistort mask and save it
//        auto maskFileName = srcImage + ".mask.png";
//        if (stlplus::is_file(maskFileName)) {
//          Image<openMVG::image::RGBColor> maskRGB, maskRGB_ud;
//          ReadImage(maskFileName.c_str(), &maskRGB);
//          UndistortImage(maskRGB, cam, maskRGB_ud, BLACK);
//          WriteImage((outputImage + ".mask.png").c_str(), maskRGB_ud);
//        }
//        // undistort depth and save it
//        auto depthFileBasename = view.second->s_Img_path;
//        auto pos = depthFileBasename.find("rgb");
//        if (pos != std::string::npos) {
//            depthFileBasename.replace(pos, 3, "depth");
//        }
//        pos = depthFileBasename.find(".png");
//        if (pos != std::string::npos) {
//            depthFileBasename.replace(pos, 4, ".pfm");
//        }
//        auto depthFileName = stlplus::create_filespec(sfm_data.s_root_path, depthFileBasename);
//        if (depthFileBasename != view.second->s_Img_path && stlplus::is_file(depthFileName)) {
//          Image<float> depth_float, depth_float_ud;
//          ReadImage(depthFileName.c_str(), &depth_float);
//          UndistortImage(depth_float, cam, depth_float_ud, 0.f);
//          WriteImage((outputImage + ".ref.pfm").c_str(), depth_float_ud);
//        }
//      }
//      else
//      {
//        // just copy image
//        stlplus::file_copy(srcImage, outputImage);
//        stlplus::file_copy(srcImage + ".mask.png", outputImage + ".mask.png");
//        stlplus::file_copy(srcImage + ".ref.pfm", outputImage + ".ref.pfm");
//      }
//      platform.poses.push_back(pose);
//      ++nPoses;
//    }
//    else
//    {
//      // image have not valid pose, so set an undefined pose
//      image.poseID = NO_ID;
//      // just copy the image
//      stlplus::file_copy(srcImage, outputImage);
//    }
//    scene.images.emplace_back(image);
//    ++my_progress_bar;
//  }
//
//  // define structure
//  scene.vertices.reserve(sfm_data.GetLandmarks().size());
//  for (const auto& vertex: sfm_data.GetLandmarks())
//  {
//    const Landmark & landmark = vertex.second;
//    MVS::Interface::Vertex vert;
//    MVS::Interface::Vertex::ViewArr& views = vert.views;
//    for (const auto& observation: landmark.obs)
//    {
//      const auto it(map_view.find(observation.first));
//      if (it != map_view.end()) {
//        MVS::Interface::Vertex::View view;
//        view.imageID = it->second;
//        view.confidence = 0;
//        views.push_back(view);
//      }
//    }
//    if (views.size() < 2)
//      continue;
//    std::sort(
//      views.begin(), views.end(),
//      [] (const MVS::Interface::Vertex::View& view0, const MVS::Interface::Vertex::View& view1)
//      {
//        return view0.imageID < view1.imageID;
//      }
//    );
//    vert.X = landmark.X.cast<float>();
//    scene.vertices.push_back(vert);
//  }
//
//  // normalize camera intrinsics
//  for (size_t p=0; p<scene.platforms.size(); ++p)
//  {
//    MVS::Interface::Platform& platform = scene.platforms[p];
//    for (size_t c=0; c<platform.cameras.size(); ++c) {
//      MVS::Interface::Platform::Camera& camera = platform.cameras[c];
//      // find one image using this camera
//      MVS::Interface::Image* pImage(nullptr);
//      for (MVS::Interface::Image& image: scene.images)
//      {
//        if (image.platformID == p && image.cameraID == c && image.poseID != NO_ID)
//        {
//          pImage = &image;
//          break;
//        }
//      }
//      if (pImage == nullptr)
//      {
//        std::cerr << "error: no image using camera " << c << " of platform " << p << std::endl;
//        continue;
//      }
//      // read image meta-data
//      ImageHeader imageHeader;
//      ReadImageHeader(stlplus::create_filespec(sOutDir, pImage->name).c_str(), &imageHeader);
//      const double fScale(1.0/std::max(imageHeader.width, imageHeader.height));
//      camera.K(0, 0) *= fScale;
//      camera.K(1, 1) *= fScale;
//      camera.K(0, 2) *= fScale;
//      camera.K(1, 2) *= fScale;
//    }
//  }
//
//  // write OpenMVS data
//  if (!MVS::ARCHIVE::SerializeSave(scene, sOutFile))
//    return false;
//
//  std::cout
//    << "Scene saved to OpenMVS interface format:\n"
//    << " #platforms: " << scene.platforms.size() << std::endl;
//    for (int i = 0; i < scene.platforms.size(); ++i)
//    {
//      std::cout << "  platform ( " << i << " ) #cameras: " << scene.platforms[i].cameras.size() << std::endl;
//    }
//  std::cout
//    << "  " << scene.images.size() << " images (" << nPoses << " calibrated)\n"
//    << "  " << scene.vertices.size() << " Landmarks\n";
//  return true;
//}

int main(int argc, char *argv[])
{
  CmdLine cmd;
  std::string sSfM_Data_Filename;
  std::string sOutFile = "";
  std::string sOutDir = "";

  cmd.add( make_option('i', sSfM_Data_Filename, "sfmdata") );
  cmd.add( make_option('o', sOutFile, "outfile") );
  cmd.add( make_option('d', sOutDir, "outdir") );

  try {
      if (argc == 1) throw std::string("Invalid command line parameter.");
      cmd.process(argc, argv);
  } catch (const std::string& s) {
      std::cerr << "Usage: " << argv[0] << '\n'
      << "[-i|--sfmdata] filename, the SfM_Data file to convert\n"
      << "[-o|--outfile] OpenMVS scene file\n"
      << "[-d|--outdir] undistorted images path\n"
      << std::endl;

      std::cerr << s << std::endl;
      return EXIT_FAILURE;
  }

  if ((0 == sOutFile.size()) && (0 == sOutDir.size()))
  {
      std::cerr << std::endl
          << "At least one of OpenMVS scene file and undistorted images path should be assigned"
          << std::endl;
  }

  if ((sOutFile.size() > 0) && (stlplus::extension_part(sOutFile) != "mvs")) {
    std::cerr << std::endl
      << "Invalid output file extension: " << sOutFile << std::endl
      << "You must use a filename with a .mvs extension." << std::endl;
      return EXIT_FAILURE;
  }

  // Read the input SfM scene
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(ALL))) {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  //TODO:test
  sfm_data.s_root_path = sfm_data.s_root_path.substr(std::string("/holostore").size());
  sfm_data.s_root_path = std::string("D:/Res") + sfm_data.s_root_path;
  ////////////////////

  if (sOutFile.size() > 0)
  {
      if (sOutDir.size() > 0)
      {
          // output undistorted images and open mvs file
          if (!exportToUndistortImagesAndOpenMVS(sfm_data, sOutFile, sOutDir))
          {
              std::cerr << std::endl
                  << "The output openMVS scene file cannot be written" << std::endl;
              return EXIT_FAILURE;
          }
      } 
      else
      {
          // output open mvs file only
          if (!exportToOpenMVS(sfm_data, sOutFile))
          {
              std::cerr << std::endl
                  << "The output openMVS scene file cannot be written" << std::endl;
              return EXIT_FAILURE;
          }
      }
  }
  else
  {
      // output undistorted images only
      if (!exportToUndistortImages(sfm_data, sOutDir))
      {
          std::cerr << std::endl
              << "Error occured during generation of undistorted images" << std::endl;
          return EXIT_FAILURE;
      }
  }

  return EXIT_SUCCESS;
}
