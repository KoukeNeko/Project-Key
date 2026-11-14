// Which dictionary and context models (tools/bigram-trainer) to use: the ones installed with the
// input method, or a newer release the settings window downloaded. Header-only so the Linux
// settings windows, which do not link the engine, show the version the engine uses.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace ProjectKey {

// The date the models in dir were released or trained (models-version.txt, from
// fetch-models.sh or train.sh), or empty when dir has none.
inline std::string ModelsVersion(const std::filesystem::path& dir) {
  std::ifstream file(dir / "models-version.txt");
  std::string version;
  file >> version;
  return version;
}

// The newest complete release under downloaded, or empty when there is none. The advanced model
// (AdvancedModel.h), which nothing bundles, loads from it as it is.
inline std::filesystem::path NewestDownload(const std::filesystem::path& downloaded) {
  std::filesystem::path newest;
  std::string newestVersion;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(downloaded, error)) {
    std::string version = ModelsVersion(entry.path());
    if (!version.empty() && version == entry.path().filename().string() && version > newestVersion) {
      newest = entry.path();
      newestVersion = version;
    }
  }
  return newest;
}

// The folder to load data.txt, bigram.bin and chars.bin from: the newest release under downloaded
// when it is newer than bundled's, else bundled. The settings windows download a release into
// downloaded/<date>.partial, write models-version.txt last and rename the folder to <date>, so a
// folder named by its own version is complete.
inline std::filesystem::path ModelsDirectory(const std::filesystem::path& bundled, const std::filesystem::path& downloaded) {
  std::filesystem::path newest = NewestDownload(downloaded);
  return !newest.empty() && ModelsVersion(newest) > ModelsVersion(bundled) ? newest : bundled;
}

}  // namespace ProjectKey
