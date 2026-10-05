/* Frozen private overlay layout from upstream 4283de1599c7da914138f82a99405d67c2c861ec. */
struct MverReferenceOverlay {
    BongoCatGL gl;
    unsigned int program;
    int textured_location;
    int image_location;
    unsigned int vao;
    unsigned int vbo;
    BongoCatPointerTexture arm;
    BongoCatPointerTexture device;
    BongoCatPointerTexture left;
    BongoCatPointerTexture right;
    BongoCatPointerTexture side;
    BongoCatMverPointerConfig geometry;
    int reference_width;
    int reference_height;
    float scale;
    float x_ratio;
    float y_ratio;
    float line_red;
    float line_green;
    float line_blue;
    bool enabled;
    bool mouse;
    bool left_handed;
    bool left_down;
    bool right_down;
    bool side_down;
};
