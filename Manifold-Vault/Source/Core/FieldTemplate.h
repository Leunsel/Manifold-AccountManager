#pragma once

#include "Core/FieldRole.h"

#include <cstddef>

namespace Bunker::Vault
{
    struct TemplateField
    {
        FieldRole Role;
        bool Secret;
    };

    struct FieldTemplate
    {
        const TemplateField* Fields;
        std::size_t Count;
    };

    FieldTemplate TemplateFor(const char* platformId);
}
