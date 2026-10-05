#pragma once

#include "xml.hpp"
#include "stream.hpp"

namespace ct
{
    inline Xml parse_xml(Stream &stream, Xml::Error *err = nullptr)
    {
        String text;
        if (!stream.read_all(text))
        {
            if (err) { *err = Xml::Error(); err->message = "erro a ler stream"; err->line = 1; err->column = 1; }
            return Xml();
        }
        return Xml::parse(text, err);
    }
}
