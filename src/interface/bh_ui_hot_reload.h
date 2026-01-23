#pragma once

#ifndef __cplusplus
#error "bh_ui_hot_reload.h is C++-only."
#endif

#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

enum class BH_UiHotReloadKind
{
    Rml,
    Rcss,
};

struct BH_UiHotReloadChange
{
    std::filesystem::path absolute_path;
    std::string rml_path; // Path relative to asset root, using forward slashes (eg. "interface/menu.rml")
    BH_UiHotReloadKind kind = BH_UiHotReloadKind::Rml;
};

// Simple polling-based hot reloader for assets/interface/*.rml and *.rcss.
// Designed to be dependency-free and portable.
class BH_UiHotReloader
{
  public:
    void Init(std::filesystem::path asset_root, std::chrono::milliseconds interval = std::chrono::milliseconds(250))
    {
        using namespace std::chrono;

        asset_root_ = std::filesystem::absolute(std::move(asset_root));
        interface_dir_ = asset_root_ / "interface";
        interval_ = interval;

        known_.clear();
        first_scan_ = true;
        next_poll_ = steady_clock::time_point{};
        initialized_ = true;
    }

    // Returns a list of changes since last poll.
    // The first call after Init() will record timestamps and return an empty list.
    std::vector<BH_UiHotReloadChange> Poll()
    {
        using namespace std::chrono;

        std::vector<BH_UiHotReloadChange> changes;

        if (!initialized_)
            return changes;

        if (interface_dir_.empty())
            return changes;

        const auto now = steady_clock::now();
        if (next_poll_ != steady_clock::time_point{} && now < next_poll_)
            return changes;

        next_poll_ = now + interval_;

        std::unordered_map<std::string, std::filesystem::file_time_type> current;

        auto push_change = [&](const std::filesystem::path &abs_path) {
            BH_UiHotReloadChange c;
            c.absolute_path = abs_path;

            // Compute relative RML path used by the FileInterface (relative to asset root).
            try
            {
                const std::filesystem::path rel = std::filesystem::relative(abs_path, asset_root_);
                c.rml_path = rel.generic_string();
            }
            catch (...)
            {
                c.rml_path.clear();
            }

            const std::string ext = abs_path.extension().string();
            std::string ext_lc;
            ext_lc.reserve(ext.size());
            for (char ch : ext)
                ext_lc.push_back((char)std::tolower((unsigned char)ch));

            if (ext_lc == ".rcss")
                c.kind = BH_UiHotReloadKind::Rcss;
            else
                c.kind = BH_UiHotReloadKind::Rml;

            changes.push_back(std::move(c));
        };

        // Scan directory for relevant files.
        try
        {
            if (std::filesystem::exists(interface_dir_))
            {
                for (const std::filesystem::directory_entry &ent :
                     std::filesystem::recursive_directory_iterator(interface_dir_))
                {
                    if (!ent.is_regular_file())
                        continue;

                    const std::filesystem::path p = ent.path();
                    std::string ext = p.extension().string();
                    for (char &ch : ext)
                        ch = (char)std::tolower((unsigned char)ch);

                    if (ext != ".rml" && ext != ".rcss")
                        continue;

                    std::filesystem::file_time_type ts{};
                    try
                    {
                        ts = std::filesystem::last_write_time(p);
                    }
                    catch (...)
                    {
                        // If timestamp retrieval fails, still treat it as a change.
                        push_change(p);
                        continue;
                    }

                    const std::string key = p.string();
                    current[key] = ts;

                    if (auto it = known_.find(key); it == known_.end())
                    {
                        push_change(p);
                    }
                    else if (it->second != ts)
                    {
                        push_change(p);
                    }
                }
            }
        }
        catch (...)
        {
            // Directory iteration failed; keep going without hot reload.
            return {};
        }

        // Detect deletions.
        for (const auto &[key, _ts] : known_)
        {
            if (current.find(key) == current.end())
            {
                push_change(std::filesystem::path(key));
            }
        }

        known_ = std::move(current);

        // First poll after init is only for timestamp capture.
        if (first_scan_)
        {
            first_scan_ = false;
            return {};
        }

        return changes;
    }

  private:
    std::filesystem::path asset_root_;
    std::filesystem::path interface_dir_;

    std::chrono::milliseconds interval_{250};
    std::chrono::steady_clock::time_point next_poll_{};

    std::unordered_map<std::string, std::filesystem::file_time_type> known_;

    bool initialized_ = false;
    bool first_scan_ = true;
};
