#include "file_operations.h"
#include "window_bounds_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <string>
#include <vector>

namespace {
    const std::filesystem::path kFixtureRoot = std::filesystem::current_path() / "tests_runtime_file_operations";
    const std::string kRoot = "/tests_runtime_file_operations";

    bool expect(bool value, const char* label) {
        if (!value) std::cerr << "FAIL: " << label << "\n";
        return value;
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    }
}

int main() {
    bool ok = true;
    const std::filesystem::path boundsStorePath = std::filesystem::current_path() / "window-bounds.cfg";
    const bool hadBoundsStore = std::filesystem::exists(boundsStorePath);
    const std::vector<uint8_t> previousBoundsStore = hadBoundsStore ? readBytes(boundsStorePath) : std::vector<uint8_t>();
    std::error_code cleanupError;
    std::filesystem::remove_all(kFixtureRoot, cleanupError);
    std::filesystem::create_directories(kFixtureRoot / "A");
    std::filesystem::create_directories(kFixtureRoot / "B");
    std::filesystem::create_directories(kFixtureRoot / "Desktop");
    std::filesystem::create_directories(kFixtureRoot / "A" / "Nested Folder" / "Empty Directory");
    const std::vector<uint8_t> fixtureBytes{0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    {
        std::ofstream output(kFixtureRoot / "A" / "sample file.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixtureBytes.data()), static_cast<std::streamsize>(fixtureBytes.size()));
    }
    {
        std::ofstream output(kFixtureRoot / "A" / "Nested Folder" / "deep file.txt", std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixtureBytes.data()), static_cast<std::streamsize>(fixtureBytes.size()));
    }

    std::string error;
    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/sample file.bin",
        gxos::files::FileClipboardOperation::Copy, error), "copy clipboard set");
    ok &= expect(gxos::files::FileOperations::CanPasteFile(kRoot + "/B", error), "copy paste enabled");
    auto result = gxos::files::FileOperations::PasteFile(kRoot + "/B");
    ok &= expect(result.success, "copy paste succeeds");
    ok &= expect(std::filesystem::exists(kFixtureRoot / "A" / "sample file.bin"), "copy leaves source");
    ok &= expect(readBytes(kFixtureRoot / "B" / "sample file.bin") == fixtureBytes, "copy preserves bytes");

    result = gxos::files::FileOperations::PasteFile(kRoot + "/Desktop");
    ok &= expect(result.success, "copy can paste to desktop destination");
    ok &= expect(readBytes(kFixtureRoot / "Desktop" / "sample file.bin") == fixtureBytes, "desktop copy preserves bytes");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/Desktop/sample file.bin",
        gxos::files::FileClipboardOperation::Move, error), "cut clipboard set");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/B");
    ok &= expect(result.success, "cut paste succeeds");
    ok &= expect(!std::filesystem::exists(kFixtureRoot / "Desktop" / "sample file.bin"), "cut removes source after destination");
    gxos::files::FileClipboardEntry cutEntry;
    ok &= expect(!gxos::files::FileClipboard::Get(cutEntry), "cut clipboard clears after success");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/sample file.bin",
        gxos::files::FileClipboardOperation::Copy, error), "conflict copy clipboard set");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/B");
    ok &= expect(result.success && result.destinationPath.find(" - Copy") != std::string::npos,
        "same-name conflict uses safe name");
    ok &= expect(readBytes(kFixtureRoot / "B" / "sample file - Copy.bin") == fixtureBytes,
        "safe-name copy preserves bytes");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/Nested Folder",
        gxos::files::FileClipboardOperation::Copy, error), "folder copy clipboard set");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/Desktop");
    ok &= expect(result.success, "folder copy paste succeeds");
    ok &= expect(std::filesystem::exists(kFixtureRoot / "A" / "Nested Folder" / "deep file.txt"),
        "folder copy leaves nested source");
    ok &= expect(readBytes(kFixtureRoot / "Desktop" / "Nested Folder" / "deep file.txt") == fixtureBytes,
        "folder copy preserves nested bytes");
    ok &= expect(std::filesystem::is_empty(kFixtureRoot / "Desktop" / "Nested Folder" / "Empty Directory"),
        "folder copy preserves empty directory");

    std::filesystem::create_directories(kFixtureRoot / "Desktop" / "Nested Folder - Copy");
    const std::vector<uint8_t> collisionBytes{0xCA, 0xFE};
    {
        std::ofstream output(kFixtureRoot / "Desktop" / "Nested Folder - Copy" / "existing.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(collisionBytes.data()), static_cast<std::streamsize>(collisionBytes.size()));
    }
    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/Nested Folder",
        gxos::files::FileClipboardOperation::Copy, error), "folder collision clipboard set");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/Desktop");
    ok &= expect(result.success && result.destinationPath.find("Nested Folder - Copy (2)") != std::string::npos,
        "folder collision uses deterministic safe name");
    ok &= expect(readBytes(kFixtureRoot / "Desktop" / "Nested Folder - Copy" / "existing.bin") == collisionBytes,
        "folder collision does not overwrite existing destination");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/Nested Folder",
        gxos::files::FileClipboardOperation::Copy, error), "descendant paste clipboard set");
    ok &= expect(!gxos::files::FileOperations::CanPasteFile(kRoot + "/A/Nested Folder", error),
        "folder paste into itself is disabled");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/A/Nested Folder");
    ok &= expect(!result.success, "folder paste into itself fails safely");
    ok &= expect(std::filesystem::exists(kFixtureRoot / "A" / "Nested Folder" / "deep file.txt"),
        "failed descendant paste preserves source");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/Nested Folder",
        gxos::files::FileClipboardOperation::Move, error), "folder cut clipboard set");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/A/Nested Folder/Empty Directory");
    ok &= expect(!result.success, "folder cut into descendant fails safely");
    ok &= expect(std::filesystem::exists(kFixtureRoot / "A" / "Nested Folder" / "deep file.txt"),
        "failed folder cut preserves source");
    ok &= expect(gxos::files::FileClipboard::Get(cutEntry),
        "failed folder cut preserves clipboard");

    result = gxos::files::FileOperations::PasteFile(kRoot + "/missing-destination");
    ok &= expect(!result.success, "unavailable destination paste fails");
    ok &= expect(gxos::files::FileClipboard::Get(cutEntry),
        "failed folder paste preserves clipboard");

    gxos::files::FileClipboard::Clear();
    ok &= expect(!gxos::files::FileOperations::CanPasteFile(kRoot + "/Desktop", error),
        "empty clipboard disables paste");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/Desktop");
    ok &= expect(!result.success, "empty clipboard paste is rejected");

    const uint64_t folderGenerationBefore = gxos::files::FileOperations::OperationGeneration();
    auto newFolder = gxos::files::FileOperations::CreateUniqueFolder(kRoot + "/Desktop");
    ok &= expect(newFolder.success && newFolder.path == kRoot + "/Desktop/New Folder",
        "hosted new folder uses default name");
    auto secondNewFolder = gxos::files::FileOperations::CreateUniqueFolder(kRoot + "/Desktop");
    ok &= expect(secondNewFolder.success && secondNewFolder.path == kRoot + "/Desktop/New Folder (2)",
        "hosted new folder collision uses suffix");
    ok &= expect(std::filesystem::is_directory(kFixtureRoot / "Desktop" / "New Folder") &&
        std::filesystem::is_directory(kFixtureRoot / "Desktop" / "New Folder (2)"),
        "hosted new folders persist on disk");
    ok &= expect(gxos::files::FileOperations::OperationGeneration() > folderGenerationBefore,
        "hosted new folder advances refresh generation");

    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/A/sample file.bin",
        gxos::files::FileClipboardOperation::Move, error), "stale clipboard set");
    std::filesystem::remove(kFixtureRoot / "A" / "sample file.bin");
    result = gxos::files::FileOperations::PasteFile(kRoot + "/B");
    ok &= expect(!result.success, "stale clipboard rejected");
    gxos::files::FileClipboardEntry staleEntry;
    ok &= expect(!gxos::files::FileClipboard::Get(staleEntry), "stale clipboard is invalidated");

    gxos::files::FileClipboard::Clear();

    // Desktop Delete uses the same collision-safe Trash operation as File
    // Explorer. Cover files, both clipboard modes, empty folders, and a
    // non-empty folder without relying on the desktop icon model.
    const std::vector<uint8_t> deleteBytes{0x11, 0x22, 0x33, 0x44};
    const std::string deleteStem = "gxos-delete-" + std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::string deleteFileName = deleteStem + ".txt";
    const std::string emptyFolderName = deleteStem + "-empty";
    const std::string treeFolderName = deleteStem + "-tree";
    const std::filesystem::path trashHostRoot = std::filesystem::current_path() / "Trash";
    const auto trashHostPath = [](const std::string& virtualPath) {
        if (virtualPath.empty() || virtualPath.front() != '/') {
            return std::filesystem::current_path() / "__invalid-trash-test-path__";
        }
        return std::filesystem::current_path() / virtualPath.substr(1);
    };
    const auto writeFixtureFile = [](const std::filesystem::path& path,
                                     const std::vector<uint8_t>& bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    };
    writeFixtureFile(kFixtureRoot / "Desktop" / deleteFileName, deleteBytes);
    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/Desktop/" + deleteFileName,
        gxos::files::FileClipboardOperation::Copy, error),
        "delete file copy clipboard set");
    auto deleteResult = gxos::files::FileOperations::MoveToTrash(kRoot + "/Desktop/" + deleteFileName);
    ok &= expect(deleteResult.success, "delete file moves to Trash");
    const std::filesystem::path firstTrashFile = trashHostPath(deleteResult.trashedPath);
    ok &= expect(!std::filesystem::exists(kFixtureRoot / "Desktop" / deleteFileName),
        "deleted file leaves Desktop");
    ok &= expect(std::filesystem::exists(firstTrashFile),
        "deleted file exists in Trash");
    ok &= expect(readBytes(firstTrashFile) == deleteBytes,
        "Trash preserves deleted file bytes");
    ok &= expect(std::filesystem::exists(firstTrashFile.string() + ".trashinfo"),
        "Trash writes restore metadata");
    gxos::files::FileClipboardEntry deletedCopy;
    ok &= expect(!gxos::files::FileClipboard::Get(deletedCopy),
        "successful delete clears copy clipboard source");

    writeFixtureFile(kFixtureRoot / "Desktop" / deleteFileName, deleteBytes);
    deleteResult = gxos::files::FileOperations::MoveToTrash(kRoot + "/Desktop/" + deleteFileName);
    const std::filesystem::path secondTrashFile = trashHostPath(deleteResult.trashedPath);
    ok &= expect(deleteResult.success && deleteResult.trashedPath.find(deleteStem + " (1).txt") != std::string::npos,
        "Trash collision uses a non-overwriting name");
    ok &= expect(readBytes(firstTrashFile) == deleteBytes && readBytes(secondTrashFile) == deleteBytes,
        "Trash collision preserves the original item");

    std::filesystem::create_directories(kFixtureRoot / "Desktop" / emptyFolderName);
    deleteResult = gxos::files::FileOperations::MoveToTrash(kRoot + "/Desktop/" + emptyFolderName);
    const std::filesystem::path emptyTrashFolder = trashHostPath(deleteResult.trashedPath);
    ok &= expect(deleteResult.success &&
        !std::filesystem::exists(kFixtureRoot / "Desktop" / emptyFolderName) &&
        std::filesystem::is_directory(emptyTrashFolder),
        "empty folder moves to Trash");

    std::filesystem::create_directories(kFixtureRoot / "Desktop" / treeFolderName / "nested");
    writeFixtureFile(kFixtureRoot / "Desktop" / treeFolderName / "root.bin", deleteBytes);
    writeFixtureFile(kFixtureRoot / "Desktop" / treeFolderName / "nested" / "child.bin", deleteBytes);
    ok &= expect(gxos::files::FileClipboard::Set(kRoot + "/Desktop/" + treeFolderName,
        gxos::files::FileClipboardOperation::Move, error),
        "delete folder cut clipboard set");
    deleteResult = gxos::files::FileOperations::MoveToTrash(kRoot + "/Desktop/" + treeFolderName);
    const std::filesystem::path treeTrashFolder = trashHostPath(deleteResult.trashedPath);
    ok &= expect(deleteResult.success &&
        !std::filesystem::exists(kFixtureRoot / "Desktop" / treeFolderName),
        "non-empty folder moves to Trash");
    ok &= expect(readBytes(treeTrashFolder / "root.bin") == deleteBytes &&
        readBytes(treeTrashFolder / "nested" / "child.bin") == deleteBytes,
        "Trash preserves non-empty folder contents");
    gxos::files::FileClipboardEntry deletedCut;
    ok &= expect(!gxos::files::FileClipboard::Get(deletedCut),
        "successful delete clears cut clipboard source");

    writeFixtureFile(kFixtureRoot / "Desktop" / "failed-delete.txt", deleteBytes);
    deleteResult = gxos::files::FileOperations::MoveToTrash(kRoot + "/Desktop/missing-delete.txt");
    ok &= expect(!deleteResult.success && std::filesystem::exists(kFixtureRoot / "Desktop" / "failed-delete.txt"),
        "failed delete leaves existing Desktop data intact");

    std::filesystem::remove_all(firstTrashFile);
    std::filesystem::remove(firstTrashFile.string() + ".trashinfo");
    std::filesystem::remove_all(secondTrashFile);
    std::filesystem::remove(secondTrashFile.string() + ".trashinfo");
    std::filesystem::remove_all(emptyTrashFolder);
    std::filesystem::remove(emptyTrashFolder.string() + ".trashinfo");
    std::filesystem::remove_all(treeTrashFolder);
    std::filesystem::remove(treeTrashFolder.string() + ".trashinfo");
    std::error_code trashCleanupError;
    if (std::filesystem::exists(trashHostRoot) && std::filesystem::is_empty(trashHostRoot, trashCleanupError)) {
        std::filesystem::remove(trashHostRoot, trashCleanupError);
    }

    gxos::gui::NormalWindowBounds bounds{40, 50, 640, 480};
    ok &= expect(gxos::gui::WindowBoundsStore::Save("app:test.one", bounds, error), "window bounds save");
    gxos::gui::NormalWindowBounds loaded;
    ok &= expect(gxos::gui::WindowBoundsStore::Load("app:test.one", loaded) &&
        loaded.x == 40 && loaded.y == 50 && loaded.w == 640 && loaded.h == 480,
        "window bounds round trip");
    ok &= expect(!gxos::gui::WindowBoundsStore::Load("app:test.two", loaded),
        "different application identity does not alias bounds");

    std::filesystem::remove_all(kFixtureRoot, cleanupError);
    if (hadBoundsStore) {
        std::ofstream restore(boundsStorePath, std::ios::binary | std::ios::trunc);
        restore.write(reinterpret_cast<const char*>(previousBoundsStore.data()),
            static_cast<std::streamsize>(previousBoundsStore.size()));
    } else {
        std::filesystem::remove(boundsStorePath, cleanupError);
    }
    std::cout << (ok ? "File operations regression tests PASS\n" : "File operations regression tests FAIL\n");
    return ok ? 0 : 1;
}
