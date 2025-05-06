#include <iostream>
#include <fstream>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <thread>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>
#include <libhackrf/hackrf.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

hackrf_device* device = nullptr;
std::ifstream infile;
const size_t BUFFER_SIZE = 262144;
bool transmitting = false;
std::atomic<bool> hopping(false);
std::thread hop_thread;
std::mutex tx_mutex;

// TX callback
int tx_callback(hackrf_transfer* transfer) {
    if (!infile.read(reinterpret_cast<char*>(transfer->buffer), transfer->valid_length)) {
        infile.clear();
        infile.seekg(0, std::ios::beg);
        infile.read(reinterpret_cast<char*>(transfer->buffer), transfer->valid_length);
    }
    return 0;
}

int main() {
    // Init HackRF
    if (hackrf_init() != HACKRF_SUCCESS) {
        std::cerr << "HackRF init failed.\n";
        return 1;
    }
    if (hackrf_open(&device) != HACKRF_SUCCESS) {
        std::cerr << "Failed to open HackRF.\n";
        hackrf_exit();
        return 1;
    }

    // Setup ImGui + GLFW
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(600, 500, "HackRF Transmitter", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // GUI state
    bool enable_freq_hop = false;
    bool show_freq_hop_popup = false;
    int hop_count = 5; // Default value
    float freq_mhz = 99.1f;
    int tx_gain = 20;
    int sample_rate = 10;
    bool amp_enabled = true;

    // Prepare HackRF
    hackrf_set_sample_rate(device, 10000000); // 10 MSPS
    std::vector<float> hop_freqs(hop_count, freq_mhz); // pre-fill with current freq
    hackrf_set_txvga_gain(device, tx_gain);
    hackrf_set_amp_enable(device, 1);

    infile.open("../../record/signal.bin", std::ios::binary);
    if (!infile) {
        std::cerr << "Failed to open signal.bin\n";
        return 1;
    }

    // hackrf_start_tx(device, tx_callback, nullptr);
    // transmitting = true;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Frequency Control");
        if (ImGui::Checkbox("Enable Freq Hop", &enable_freq_hop)) {
            show_freq_hop_popup = enable_freq_hop; // show popup when checkbox is activated
        }
        ImGui::InputFloat("Freq (MHz)", &freq_mhz, 0.1f, 1.0f, "%.3f");
        ImGui::SliderInt("TX Gain", &tx_gain, 0, 47);
        ImGui::SliderInt("SAMPLE rate", &sample_rate, 1, 20);
        if (ImGui::Checkbox("Enable Amp", &amp_enabled)) {
            hackrf_set_amp_enable(device, amp_enabled ? 1 : 0);

        }
        
        

        if (ImGui::Button("Submit",ImVec2(200, 40)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            system("clear");
            
            uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
            hackrf_set_freq(device, freq_hz);
            hackrf_set_sample_rate(device, sample_rate * 1000000);
            hackrf_set_txvga_gain(device, tx_gain);
            
            std::cout << "Frequency set to " << freq_mhz << " MHz\n";
            std::cout << "TX Gain set to " << tx_gain << "\n";
            std::cout << "Sample rate set to " << sample_rate << "\n";
            std::cout << "Amp " << (amp_enabled ? "enabled" : "disabled") << "\n";
        }

        if (!transmitting && ImGui::Button("Start Transmitting", ImVec2(200, 40))) {
            infile.clear();
            infile.seekg(0, std::ios::beg);
            if (!infile.is_open()) {
                infile.open("../../record/signal.bin", std::ios::binary);
            }
            if (infile) {
                hackrf_start_tx(device, tx_callback, nullptr);
                transmitting = true;
                std::cout << "Started transmitting...\n";
        
                if (enable_freq_hop && hop_freqs.size() > 0) {
                    hopping = true;
                    hop_thread = std::thread([&]() {
                        size_t i = 0;
                        while (hopping && transmitting) {
                            {
                                std::lock_guard<std::mutex> lock(tx_mutex);
                                uint64_t freq_hz = static_cast<uint64_t>(hop_freqs[i] * 1e6);
                                hackrf_set_freq(device, freq_hz);
                                std::cout << "[Hop] Set frequency to " << hop_freqs[i] << " MHz\n";
                            }
                            i = (i + 1) % hop_freqs.size();
                            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // hop interval
                        }
                    });
                } else {
                    // Single frequency mode
                    uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
                    hackrf_set_freq(device, freq_hz);
                }
            } else {
                std::cerr << "signal.bin file not found.\n";
            }
        }
        
        if (transmitting && ImGui::Button("Stop Transmitting", ImVec2(200, 40))) {
            hopping = false;
            if (hop_thread.joinable()) {
                hop_thread.join();
            }
            hackrf_stop_tx(device);
            transmitting = false;
            std::cout << "Transmission stopped.\n";
        }

        if (show_freq_hop_popup) {
            ImGui::OpenPopup("Freq Hop Settings");
            show_freq_hop_popup = false; // prevent reopening constantly
        }
        
        if (ImGui::BeginPopupModal("Freq Hop Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configure frequency hopping:");
            // ImGui::InputInt("Hop Count", &hop_count);
            // ImGui::Separator();
            if (ImGui::InputInt("Hop Count", &hop_count)) {
                if (hop_count < 1) hop_count = 1;
                hop_freqs.resize(hop_count, freq_mhz); // Resize while keeping defaults
            }
            ImGui::Separator();
            
            for (int i = 0; i < hop_count; ++i) {
                ImGui::InputFloat(("Hop " + std::to_string(i + 1)).c_str(), &hop_freqs[i], 0.1f, 1.0f, "%.3f");
            }
            
            
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                enable_freq_hop = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (enable_freq_hop) {
            ImGui::Text("Frequency Hopping:");
            for (int i = 0; i < static_cast<int>(hop_freqs.size()); ++i) {
                ImGui::BulletText("Hop %d: %.3f MHz", i + 1, hop_freqs[i]);
            }
        }

        ImGui::End();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){
            break;
        }
    }

    // Cleanup
    if (transmitting) {
        hackrf_stop_tx(device);
        infile.close();
    }
    hackrf_close(device);
    hackrf_exit();

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Transmission ended.\n";
    return 0;
}