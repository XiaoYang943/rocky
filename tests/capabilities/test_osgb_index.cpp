#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBIndex.h"
#include "../../src/render/import/osgb/OSGBFiles.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <fstream>
#include <filesystem>
#include <memory>

using namespace geostudio::native::io::detail;
namespace fs = std::filesystem;
namespace
{
const std::string metadata = "<ModelMetadata><SRS>EPSG:4545</SRS><SRSOrigin>607429,3938520,0</SRSOrigin></ModelMetadata>";
fs::path base() { return fs::u8path(GN_OSGB_FIXTURES) / "index-tests"; }

// 仅创建本测试管理的固定小文件；已存在的内容必须一致，禁止覆盖未知原件。
void fixture(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    if (fs::exists(path)) REQUIRE(readBoundedFile(path, 1024 * 1024) == bytes);
    else
    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
    }
}
fs::path source(const std::string& name = u8"中文 inputs")
{
    const auto root = base() / fs::u8path(name) / "OSGB";
    fixture(root / "metadata.xml", metadata);
    // 索引不得依赖 OSGB 解码，所以使用不可解码但身份明确的小文件。
    fixture(root / "Data" / "Tile_A" / "Tile_A.osgb", "root-one");
    return root;
}
fs::path createShp(const std::string& name, const fs::path& root, double originX = 607429,
    const std::string& rootName = "Tile_A", bool duplicate = false, int geometryEpsg = 4545)
{
    GDALAllRegister();
    const auto path = base() / fs::u8path(u8"中文 inputs") / "index" / fs::u8path(name + ".shp");
    fs::create_directories(path.parent_path());
    auto driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    REQUIRE(driver);
    const char* openOptions[] = {"ENCODING=CP936", nullptr};
    const bool exists = fs::exists(path);
    std::unique_ptr<GDALDataset, decltype(&GDALClose)> dataset(exists ? static_cast<GDALDataset*>(
        GDALOpenEx(path.u8string().c_str(), GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr, openOptions, nullptr)) :
        driver->Create(path.u8string().c_str(), 0, 0, 0, GDT_Unknown, nullptr), GDALClose);
    REQUIRE(dataset);
    OGRSpatialReference srs;
    REQUIRE(srs.importFromEPSG(geometryEpsg) == OGRERR_NONE);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    char encoding[] = "ENCODING=CP936";
    char* options[] = {encoding, nullptr};
    auto layer = exists ? dataset->GetLayer(0) : dataset->CreateLayer(name.c_str(), &srs, wkbPolygon, options);
    REQUIRE(layer);
    auto cpg = path;
    cpg.replace_extension(".cpg");
    if (exists && layer->GetFeatureCount() > 0)
    {
        REQUIRE(layer->GetLayerDefn()->GetFieldCount() == 6);
        REQUIRE(layer->GetFeatureCount() == (duplicate ? 2 : 1));
        REQUIRE(readBoundedFile(cpg, 256) == "GBK");
        return path;
    }
    // 只修复本测试拥有的空半成品，不删除 SHP/DBF/SHX/PRJ；未知结构必须失败。
    REQUIRE(layer->GetFeatureCount() == 0);
    REQUIRE(layer->GetLayerDefn()->GetFieldCount() == 0);
    for (const auto* fieldName : {"SJY", "GWMC", "EPSG"})
    {
        OGRFieldDefn field(fieldName, OFTString);
        field.SetWidth(254);
        REQUIRE(layer->CreateField(&field) == OGRERR_NONE);
    }
    for (const auto* fieldName : {"YDZBX", "YDZBY", "YDZBZ"})
    {
        OGRFieldDefn field(fieldName, OFTReal);
        field.SetWidth(24); field.SetPrecision(8);
        REQUIRE(layer->CreateField(&field) == OGRERR_NONE);
    }
    for (int i = 0; i < (duplicate ? 2 : 1); ++i)
    {
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> feature(
            OGRFeature::CreateFeature(layer->GetLayerDefn()), OGRFeature::DestroyFeature);
        feature->SetField("SJY", (fs::absolute(root) / "Data" / "Tile_A").u8string().c_str());
        feature->SetField("GWMC", rootName.c_str());
        feature->SetField("EPSG", "4545");
        feature->SetField("YDZBX", originX);
        feature->SetField("YDZBY", 3938520.0);
        feature->SetField("YDZBZ", 0.0);
        OGRLinearRing ring;
        ring.addPoint(607429, 3938520); ring.addPoint(607529, 3938520);
        ring.addPoint(607529, 3938620); ring.addPoint(607429, 3938620); ring.addPoint(607429, 3938520);
        OGRPolygon polygon;
        polygon.addRing(&ring);
        REQUIRE(feature->SetGeometry(&polygon) == OGRERR_NONE);
        REQUIRE(layer->CreateFeature(feature.get()) == OGRERR_NONE);
    }
    dataset.reset();
    const auto previousEncoding = readBoundedFile(cpg, 256);
    REQUIRE((previousEncoding == "GBK" || previousEncoding == "CP936"));
    // CP936 编码的真实中文 DBF 配上 GBK 声明，覆盖原始资料采用的别名。
    std::ofstream cpgOutput(cpg, std::ios::binary | std::ios::trunc);
    cpgOutput << "GBK";
    REQUIRE(cpgOutput.good());
    return path;
}

// 临时改变一个已验证的测试自有输入，并在所有异常路径恢复同一文件及时间身份。
struct ChangedFixture
{
    fs::path path;
    std::string previous;
    fs::file_time_type time;
    explicit ChangedFixture(const fs::path& p) : path(p), previous(readBoundedFile(p, 1024)), time(fs::last_write_time(p))
    {
        REQUIRE(previous == "root-one");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "root-two";
        output.close();
        fs::last_write_time(path, time);
    }
    ~ChangedFixture()
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << previous;
        output.close();
        std::error_code error;
        fs::last_write_time(path, time, error);
    }
};
}

TEST_CASE("OSGB index reads only metadata and preserves absent three-dimensional bounds", "[osgb][osgb-index]")
{
    const auto root = source();
    const auto index = readDatasetIndex(root);
    REQUIRE(index.roots.size() == 1);
    CHECK(index.roots[0].relativePath == "Data/Tile_A/Tile_A.osgb");
    CHECK(index.roots[0].source.size == 8);
    CHECK(index.roots[0].source.sha256.empty());
    CHECK_FALSE(index.roots[0].footprint);
    CHECK_FALSE(index.roots[0].localEcefSphere);
    REQUIRE(index.ecefAnchor);
    CHECK(index.srs == "EPSG:4545");
    CHECK(readDatasetIndex(root / "metadata.xml").roots.size() == 1);
    CHECK_FALSE(fs::exists(root / "index.json"));
    CHECK(sha256Bytes("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(root / "Data/Tile_A/Tile_A.osgb") == sha256Bytes("root-one"));
}

TEST_CASE("OSGB SHP imports projected XY and validates relocation and metadata", "[osgb][osgb-index]")
{
    const auto root = source();
    const auto shp = createShp("valid", root);
    const auto index = readDatasetIndex(shp);
    REQUIRE(index.roots.size() == 1);
    REQUIRE(index.roots[0].footprint);
    CHECK(index.roots[0].footprint->xy[0] == 607429);
    CHECK(index.roots[0].footprint->xy[1] == 3938520);
    CHECK_FALSE(index.roots[0].localEcefSphere);
    CHECK(index.roots[0].source.sha256.empty());
    IndexReadOptions relocated;
    relocated.sourceRootOverride = source(u8"重定位 数据");
    const auto moved = readDatasetIndex(shp, relocated);
    CHECK(moved.sourceRoot == fs::canonical(relocated.sourceRootOverride));
    CHECK(moved.roots[0].relativePath == index.roots[0].relativePath);
    CHECK_THROWS(readDatasetIndex(createShp("bad-origin", root, 607430)));
    CHECK_THROWS(readDatasetIndex(createShp("bad-srs", root, 607429, "Tile_A", false, 3857)));
    CHECK_THROWS(readDatasetIndex(createShp("bad-path", root, 607429, "../escape")));
    CHECK_THROWS(readDatasetIndex(createShp("duplicate", root, 607429, "Tile_A", true)));
}

TEST_CASE("OSGB bounds cache requires strong source and coordinate identity", "[osgb][osgb-index]")
{
    const auto root = source();
    const auto cache = base() / "cache";
    auto index = readDatasetIndex(root);
    index.roots[0].localEcefSphere = std::array<double, 4>{{1, 2, 3, 10}};
    CHECK_THROWS(publishDatasetIndex(index, cache));
    index.roots[0].source.sha256 = sha256File(root / fs::u8path(index.roots[0].relativePath));
    publishDatasetIndex(index, cache);
    IndexReadOptions options;
    options.cacheDirectory = cache;
    auto hit = readDatasetIndex(root, options);
    REQUIRE(hit.roots[0].localEcefSphere);
    CHECK((*hit.roots[0].localEcefSphere)[3] == 10);
    options.coordinatePolicy = "different-policy";
    CHECK_FALSE(readDatasetIndex(root, options).roots[0].localEcefSphere);
    options.coordinatePolicy = "ecef-v1";
    {
        ChangedFixture changed(root / "Data/Tile_A/Tile_A.osgb");
        CHECK_FALSE(readDatasetIndex(root, options).roots[0].localEcefSphere);
        CHECK_THROWS(publishDatasetIndex(index, cache));
    }
    CHECK(readDatasetIndex(root, options).roots[0].localEcefSphere);
}

TEST_CASE("OSGB index publication preserves inputs and rejects unmanaged destinations", "[osgb][osgb-index]")
{
    const auto root = source();
    const auto index = readDatasetIndex(root);
    CHECK_THROWS(publishDatasetIndex(index, root / "cache"));
    CHECK_THROWS(publishDatasetIndex(index, root.parent_path()));
    CHECK_FALSE(pathWithin(root / "../escape", root));
    const auto unmanaged = base() / "unmanaged";
    fixture(unmanaged / "index.json", "{\"human\":true}");
    CHECK_THROWS(publishDatasetIndex(index, unmanaged));
    CHECK(readBoundedFile(unmanaged / "index.json", 1024) == "{\"human\":true}");
    const auto locked = base() / "locked";
    fixture(locked / "index.json.lock", "external-writer");
    CHECK_THROWS(publishDatasetIndex(index, locked));
    CHECK_FALSE(fs::exists(locked / "index.json"));
    CHECK(readBoundedFile(locked / "index.json.lock", 1024) == "external-writer");
    const auto pending = base() / "pending";
    fixture(pending / "index.json.pending", "preserved-failed-output");
    CHECK_THROWS(publishDatasetIndex(index, pending));
    CHECK_FALSE(fs::exists(pending / "index.json"));
    CHECK(readBoundedFile(pending / "index.json.pending", 1024) == "preserved-failed-output");
    CHECK(readBoundedFile(root / "metadata.xml", 1024) == metadata);
    CHECK(readBoundedFile(root / "Data/Tile_A/Tile_A.osgb", 1024) == "root-one");
}
