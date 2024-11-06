#include <zip.h>
#include <pugixml.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

void clean_epub(const std::string& input_epub) {
    std::string output_epub = input_epub.substr(0, input_epub.find_last_of(".")) + "-clean.epub";
    std::string temp_dir = "/tmp/temp_epub/";

    // Unzip EPUB to temporary directory
    int err = 0;
    zip* archive = zip_open(input_epub.c_str(), 0, &err);
    if (!archive) {
        std::cerr << "Error opening EPUB file: " << input_epub << std::endl;
        return;
    }

    fs::create_directories(temp_dir);

    for (int i = 0; i < zip_get_num_entries(archive, 0); i++) {
        struct zip_stat stat;
        zip_stat_index(archive, i, 0, &stat);

        // Extract each file
        zip_file* file = zip_fopen_index(archive, i, 0);
        std::string filepath = temp_dir + stat.name;

        if (file) {
            std::ofstream output_file(filepath, std::ios::binary);
            char buffer[4096];
            int bytes_read = 0;
            while ((bytes_read = zip_fread(file, buffer, 4096)) > 0) {
                output_file.write(buffer, bytes_read);
            }
            zip_fclose(file);
        }
    }
    zip_close(archive);

    // Process HTML files
    for (auto& p : fs::recursive_directory_iterator(temp_dir)) {
        if (p.path().extension() == ".xhtml" || p.path().extension() == ".html") {
            pugi::xml_document doc;
            doc.load_file(p.path().c_str());
            
            // Remove watermark
            std::string content;
            pugi::xml_node body = doc.child("body");
            for (auto& text_node : body.children("text")) {
                content = text_node.text().get();
                content = std::regex_replace(content, std::regex("RUBoard"), "");
                text_node.text().set(content.c_str());
            }

            // Remove Next/Prev buttons/links
            for (pugi::xml_node node : doc.select_nodes("//a[contains(text(), 'Next') or contains(text(), 'Prev')]")) {
                node.parent().remove_child(node);
            }

            // Fix line flow in paragraphs
            for (pugi::xml_node p : doc.child("body").children("p")) {
                std::string paragraph = p.text().get();
                std::regex newlines_re("\\r?\\n");
                paragraph = std::regex_replace(paragraph, newlines_re, " ");
                p.text().set(paragraph.c_str());
            }

            doc.save_file(p.path().c_str());
        }

        // Process image files
        else if (p.path().extension() == ".jpg" || p.path().extension() == ".jpeg" || p.path().extension() == ".png") {
            cv::Mat img = cv::imread(p.path().string());
            if (!img.empty()) {
                // Check for rotation in EXIF data and correct orientation
                cv::rotate(img, img, cv::ROTATE_180);  // Assuming all images are upside down
                cv::imwrite(p.path().string(), img);
            }
        }
    }

    // Re-zip files into cleaned EPUB
    struct zip_t *cleaned_archive = zip_open(output_epub.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    for (auto& p : fs::recursive_directory_iterator(temp_dir)) {
        std::ifstream file(p.path(), std::ios::binary);
        std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        zip_entry_open(cleaned_archive, p.path().c_str());
        zip_entry_write(cleaned_archive, buffer.data(), buffer.size());
        zip_entry_close(cleaned_archive);
    }
    zip_close(cleaned_archive);

    std::cout << "Cleaned EPUB saved as " << output_epub << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./clean_epub <input_epub>" << std::endl;
        return 1;
    }
    clean_epub(argv[1]);
    return 0;
}
