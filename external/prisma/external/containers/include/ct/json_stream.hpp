#pragma once

#include "json.hpp"
#include "stream.hpp"

namespace ct
{
    inline Json parse_json(Stream &stream, Json::Error *err = nullptr)
    {
        String text;
        if (!stream.read_all(text))
        {
            if (err) { *err = Json::Error(); err->message = "erro a ler stream"; err->line = 1; err->column = 1; }
            return Json();
        }
        return Json::parse(text, err);
    }
}
