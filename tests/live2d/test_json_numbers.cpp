#include "test.h"
#include <CubismFramework.hpp>
#include <ICubismAllocator.hpp>
#include <Motion/CubismExpressionMotion.hpp>
#include <Motion/CubismMotion.hpp>
#include <Utils/CubismJson.hpp>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>
#ifdef _WIN32
#include <malloc.h>
#endif

using namespace Live2D::Cubism::Framework;
int bongo_cat_test_failures = 0;

class TestAllocator : public ICubismAllocator {
public:
    void* Allocate(csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(csmSizeType size, csmUint32 alignment) override {
#ifdef _WIN32
        return _aligned_malloc(size, alignment);
#else
        void* memory = nullptr;
        return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
#endif
    }
    void DeallocateAligned(void* memory) override {
#ifdef _WIN32
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }
};

static Utils::CubismJson* parse(const std::string& text) {
    return Utils::CubismJson::Create(
        reinterpret_cast<const csmByte*>(text.data()),
        static_cast<csmSizeInt>(text.size()));
}

static void check_number(const char* number, float expected) {
    for (const char* suffix : {",\"other\":0\n}", "}", " \t\r\n}"}) {
        auto* json = parse(std::string("{\"value\":") + number + suffix);
        CHECK(json != nullptr);
        if (json) {
            CHECK(std::fabs(json->GetRoot()["value"].ToFloat() - expected)
                <= std::fabs(expected) * 1e-6f);
            Utils::CubismJson::Delete(json);
        }
    }
}

int main() {
    TestAllocator allocator;
    CHECK(CubismFramework::StartUp(&allocator));
    CubismFramework::Initialize();
    check_number("-2.980232238769531e-7", -2.980232238769531e-7f);
    check_number("2.5E+2", 250.0f);
    check_number("1e0", 1.0f);
    check_number("-12.5", -12.5f);
    check_number("0", 0.0f);
    const csmByte bounded[] = {'[', '1', 'e', '0', ']'};
    auto* bounded_json = Utils::CubismJson::Create(bounded, sizeof(bounded));
    CHECK(bounded_json != nullptr);
    if (bounded_json) {
        CHECK(bounded_json->GetRoot()[0].ToFloat() == 1.0f);
        Utils::CubismJson::Delete(bounded_json);
    }
    auto* string_json = parse(R"({"Id":"Param1e0","Value":"1e+2"})");
    CHECK(string_json != nullptr);
    if (string_json) {
        CHECK(string_json->GetRoot()["Id"].GetString() == "Param1e0");
        CHECK(string_json->GetRoot()["Value"].GetString() == "1e+2");
        Utils::CubismJson::Delete(string_json);
    }
    for (const char* invalid : {"1e", "1e+", "1.2.3", "--1", "01"}) {
        auto* json = parse(std::string("{\"value\":") + invalid + "}");
        CHECK(json == nullptr);
        if (json) Utils::CubismJson::Delete(json);
    }
    const std::string expression = R"({"Type":"Live2D Expression","Parameters":[{"Id":"Param707","Value":-2.980232238769531e-7,"Blend":"Add"}]})";
    auto* json = parse(expression);
    CHECK(json != nullptr);
    if (json) {
        Utils::CubismJson::Delete(json);
        auto* motion = CubismExpressionMotion::Create(
            reinterpret_cast<const csmByte*>(expression.data()),
            static_cast<csmSizeInt>(expression.size()));
        CHECK(motion != nullptr);
        if (motion) {
            const auto parameters = motion->GetExpressionParameters();
            CHECK(parameters.GetSize() == 1);
            if (parameters.GetSize() == 1) {
                CHECK(parameters[0].Value == -2.980232238769531e-7f);
                CHECK(parameters[0].BlendType == CubismExpressionMotion::Additive);
            }
            ACubismMotion::Delete(motion);
        }
    }
    const std::string motion_json = R"({"Version":3,"Meta":{"Duration":1e0,"Fps":3E+1,"Loop":false,"AreBeziersRestricted":false,"CurveCount":1,"TotalSegmentCount":1,"TotalPointCount":2,"UserDataCount":0,"TotalUserDataSize":0},"Curves":[{"Target":"Parameter","Id":"ParamAngleX","Segments":[0,0,0,1e0,2.5e-1]}]})";
    json = parse(motion_json);
    CHECK(json != nullptr);
    if (json) {
        CHECK(json->GetRoot()["Curves"][0]["Segments"][4].ToFloat() == 0.25f);
        Utils::CubismJson::Delete(json);
        auto* motion = CubismMotion::Create(
            reinterpret_cast<const csmByte*>(motion_json.data()),
            static_cast<csmSizeInt>(motion_json.size()));
        CHECK(motion != nullptr);
        if (motion) {
            CHECK(motion->GetDuration() == 1.0f);
            ACubismMotion::Delete(motion);
        }
    }
    CubismFramework::Dispose();
    CubismFramework::CleanUp();
    return bongo_cat_test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
