#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "base.hpp"
#include <utils.hpp>

extern "C" Base::BaseClass* GetRenderer();
namespace Base {
    struct Renderer : BaseClass {
        struct Vertex {
            /// Affects parralax and the order it's drawn at.
            int8_t depth = 0;
            /// If two have the same layer then it's up to this one to decide what's in front and what's behind
            int8_t layer = 0;
            constexpr inline int16_t get_z_index() const noexcept {
                typedef std::numeric_limits<typeof(layer)> limits;
                constexpr auto layer_factor = mth::pow(2, sizeof(typeof(layer)) * 8);
                return (int16_t)depth * layer_factor + (int16_t)layer;
            }

            float x, y;
        };
        struct ColouredVertex : Vertex {
            uint32_t rgba;
        };
        struct TexturedVertex : Vertex {
            uint8_t textureid;
            uint16_t uvx, uvy;
        };

        struct {
            std::vector<ColouredVertex> coloured{};
            std::vector<TexturedVertex> textured{};
        } worldtris;

        struct {
            std::vector<ColouredVertex> coloured{};
            std::vector<TexturedVertex> textured{};
        } ui_tris;

        /**
         * @note MUST BE RAW BYTES FROM PNG
         * @brief Stores the atlas.
         * This is run before `init()` and cannot be run after `init()`
         *
         * The atlas ID has 255 reserved as "not found"
         */
        virtual void addAtlasFromData(std::string_view atlas_name, std::span<const unsigned char> bytes) = 0;
        virtual uint8_t getAtlasId(std::string_view atlas_name) = 0;
        struct Camera {
            float x = 0, y = 0;
            // zoom is how many tiles can fit into the screen width/height
            // i.e zoom=1 means that a square one unit big fits the whole screen,
            // while zoom=10 means that it a square 10 units big fits the whole screen
            // so that it's consistent for window size.
            double zoom = 10;
            double _real_zoom = 0;

            double zoom_speed = 15;
            double zoom_snap_distance = 0.01;

            // uizoom is how many ui pixels can fit into (min(screen width, screen height))
            // so that it's also consistent for window size
            double uizoom = 1;
            inline void update_zoom(double dt) {
                //_real_zoom = zoom;
                _real_zoom += (zoom - _real_zoom) / 2 * dt * zoom_speed;
                if (mth::abs(zoom - _real_zoom) <= zoom_snap_distance) _real_zoom = zoom;
            }
        } camera;

        protected:
        friend int ::main();

        public:
        inline static std::unique_ptr<Renderer> get() {
            std::unique_ptr<Renderer> retval = nullptr;
            retval.reset(dynamic_cast<Renderer*>(GetRenderer()));
            return retval;
        };
    };
}  // namespace Base
