Release notes, read by the release workflow.

- `release-notes/vX.Y.Z.md`: the GitHub release's notes for tag `vX.Y.Z`, in English and then in Traditional Chinese
  (a release has one body, so both are in the one file, the same sections in the same order).
  Without one, the release falls back to notes generated from the commits.
  Written before the tag is pushed; a run again on a release that exists puts the file's notes on it as well.

Project Key is published only on GitHub, in the Homebrew tap (`KoukeNeko/homebrew-tap`: the `project-key` cask for
macOS, the `fcitx5-projectkey` formula for Linux) and in the Scoop bucket (`KoukeNeko/scoop-bucket`), so there is no
store "What's New" to keep; the cask, the formula and the manifest are written by `.github/workflows/release.yml`.

To release: set the version in `platforms/macos/Resources/Info.plist`,
`platforms/windows/Sources/TextService/ProjectKey.rc` and `platforms/linux/CMakeLists.txt`, add `release-notes/vX.Y.Z.md`, commit, then push the tag `vX.Y.Z`, or, with the commit pushed, create the release with
`gh release create vX.Y.Z --notes-file distribution/release-notes/vX.Y.Z.md`: it creates the tag, and the workflow then
uploads the zips, the macOS pkg and `SHA256SUMS` to that release. The builds bundle the
dictionary and context models that `tools/bigram-trainer/fetch-models.sh` pins; to ship newly trained ones, publish
them first as `tools/bigram-trainer/fetch-models.sh` describes.
