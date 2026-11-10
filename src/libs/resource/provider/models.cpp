/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/resource/provider/models.h"

#include "reone/graphics/format/mdlmdxreader.h"
#include "reone/graphics/model.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::graphics;

namespace reone {

namespace resource {

void Models::clear() {
    _cache.clear();
}

std::shared_ptr<Model> Models::get(const std::string &resRef) {
    if (resRef.empty()) {
        return nullptr;
    }
    return entry(resRef).value;
}

const Models::ModelCache::Entry &Models::entry(const std::string &resRef) {
    auto lcResRef = boost::to_lower_copy(resRef);
    return _cache.get(lcResRef, [this, &lcResRef](ResourceReads &reads) {
        return doGet(lcResRef, reads);
    });
}

std::shared_ptr<Model> Models::doGet(const std::string &resRef, ResourceReads &reads) {
    debug("Load model " + resRef, LogChannel::Graphics);

    auto mdlRes = reads.find(_resources, ResourceId(resRef, ResType::Mdl));
    auto mdxRes = reads.find(_resources, ResourceId(resRef, ResType::Mdx));
    std::shared_ptr<Model> model;

    if (mdlRes && mdxRes) {
        auto mdl = MemoryInputStream(mdlRes->data);
        auto mdx = MemoryInputStream(mdxRes->data);
        auto reader = MdlMdxReader(mdl, mdx, _statistic);
        try {
            reader.load();
            model = reader.model();
            if (!model->superModelName().empty()) {
                // A model is only as current as the supermodel it was built on.
                auto &superModel = entry(model->superModelName());
                reads.add(superModel.reads);
                model->setSuperModel(superModel.value);
            }
            model->init();
        } catch (const ValidationException &e) {
            error(str(boost::format("Error loading model %s: %s") % resRef % std::string(e.what())), LogChannel::Graphics);
        }
    }

    return model;
}

} // namespace resource

} // namespace reone
