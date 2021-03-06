// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2015 Pierre Moulon.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OPENMVG_SFM_SFM_DATA_UTILS_HPP
#define OPENMVG_SFM_SFM_DATA_UTILS_HPP

#include <vector>
#include <string>
#include "openMVG/numeric/eigen_alias_definition.hpp"

namespace openMVG {
namespace sfm {

struct SfM_Data;

// Group camera models that share common camera properties
// It modifies the intrinsic_id of the view field and change the sfm_data.intrinsics length
// Grouping is simplified by using a hash function over the camera intrinsics
// - it allow to merge camera model that share common camera parameters & image sizes
void GroupSharedIntrinsics(SfM_Data & sfm_data);

// Sort views using its filename, new view_id starts with 0
// Then remove invalid views
void SortAndCleanSfMData(SfM_Data & sfm_data);

// Reorder views using sorted view_names, new view_id starts with 0
// If a view is not found in sfm_data, the corresponding id will not be used
void SortAndCleanSfMData(SfM_Data & sfm_data, std::vector<std::string> & view_names);

// Transform poses and landmarks using given transform matrix
void TransformSfMData(const Mat4 & transform, SfM_Data & sfm_data);

} // namespace sfm
} // namespace openMVG

#endif // OPENMVG_SFM_SFM_DATA_UTILS_HPP
