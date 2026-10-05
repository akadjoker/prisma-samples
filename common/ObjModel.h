#pragma once

#include "GltfModel.h"

namespace zenapp
{

bool loadObj(const char* path, GltfModel* out, bool allTangents = false);

} // namespace zenapp
