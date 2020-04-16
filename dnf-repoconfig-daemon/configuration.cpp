/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "configuration.hpp"

#include "../libdnf/dnf-context.h" // for find_base_arch
#include "../libdnf/utils/os-release.hpp" // for getOsReleaseData

#include <glob.h>
#include <iostream>
#include <rpm/rpmlib.h>

void Configuration::read_configuration()
{
    set_substitutions();
    read_main_config();
    read_repo_configs();
}

void Configuration::set_substitutions()
{
    const char *arch;
    rpmGetArchInfo(&arch, NULL);
    substitutions["arch"] = std::string(arch);
    const auto basearch = find_base_arch(arch);
    substitutions["basearch"] = basearch ? std::string(basearch) : "";
    //TODO do we have reliable way to detect releasever?
    auto osdata = libdnf::getOsReleaseData();
    std::string releasever="";
    if (osdata.count("VERSION_ID")) {
        releasever = osdata.at("VERSION_ID");
    }
    substitutions["releasever"] = releasever;
}

void Configuration::read_main_config()
{
    std::unique_ptr<libdnf::ConfigParser> main_parser(new libdnf::ConfigParser);
    main_parser->setSubstitutions(substitutions);
    auto main_config_path = cfg_main.config_file_path().getValue();
    main_parser->read(main_config_path);
    const auto & cfgParserData = main_parser->getData();
    const auto cfgParserDataIter = cfgParserData.find("main");
    if (cfgParserDataIter != cfgParserData.end()) {
        auto optBinds = cfg_main.optBinds();
        for (const auto &opt : cfgParserDataIter->second) {
            const auto optBindsIter = optBinds.find(opt.first);
            if (optBindsIter != optBinds.end()) {
                optBindsIter->second.newString(
                    libdnf::Option::Priority::MAINCONFIG,
                    main_parser->getSubstitutedValue("main", opt.first));
            }
        }
    }
    read_repos(main_parser.get(), main_config_path);
    config_parsers[std::move(main_config_path)] = std::move(main_parser);
}

void Configuration::read_repos(const libdnf::ConfigParser *repo_parser, const std::string &file_path)
{
    const auto & cfgParserData = repo_parser->getData();
    for (const auto & cfgParserDataIter : cfgParserData) {
        if (cfgParserDataIter.first != "main") {
            auto section = cfgParserDataIter.first;
            std::unique_ptr<libdnf::ConfigRepo> cfgRepo(new libdnf::ConfigRepo(cfg_main));
            auto optBinds = cfgRepo->optBinds();
            for (const auto &opt : cfgParserDataIter.second) {
                if (opt.first[0] == '#') {
                    continue;
                }
                const auto optBindsIter = optBinds.find(opt.first);
                if (optBindsIter != optBinds.end()) {
                    optBindsIter->second.newString(
                        libdnf::Option::Priority::REPOCONFIG,
                        repo_parser->getSubstitutedValue(section, opt.first));
                }
            }
            std::unique_ptr<RepoInfo> repoinfo(new RepoInfo());
            repoinfo->file_path = std::string(file_path);
            repoinfo->repoconfig = std::move(cfgRepo);
            repos[std::move(section)] = std::move(repoinfo);
        }
    }
}

void Configuration::read_repo_configs()
{
    for (auto reposDir : cfg_main.reposdir().getValue()) {
        const std::string pattern = reposDir + "/*.repo";
        glob_t globResult;
        glob(pattern.c_str(), GLOB_MARK, NULL, &globResult);
        for (size_t i = 0; i < globResult.gl_pathc; ++i) {
            std::unique_ptr<libdnf::ConfigParser> repo_parser(new libdnf::ConfigParser);
            std::string file_path=globResult.gl_pathv[i];
            repo_parser->setSubstitutions(substitutions);
            repo_parser->read(file_path);
            read_repos(repo_parser.get(), file_path);
            config_parsers[std::string(file_path)] = std::move(repo_parser);
        }
        globfree(&globResult);
    }
}

Configuration::RepoInfo* Configuration::find_repo(const std::string &repoid) {
    auto repo_iter=repos.find(repoid);
    if (repo_iter == repos.end()) {
        return NULL;
    }
    return repo_iter->second.get();
}

libdnf::ConfigParser* Configuration::find_parser(const std::string &file_path) {
    auto parser_iter=config_parsers.find(file_path);
    if (parser_iter == config_parsers.end()) {
        return NULL;
    }
    return parser_iter->second.get();
}