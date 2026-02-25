#pragma once
#include <filesystem>
#include <string>

class FString;
class UWorld;

namespace NS_SceneManagerData
{
struct FSceneData;
}

class SceneManager
{
public:
    // TODO: AssetManager 만들면 Manager에서 필요한 값을 가져오는걸로 바꾸기

    /**
     * Json형식으로 저장된 World파일을 불러옵니다.
     *
     * Supports all actor and component types including:
     * - Static Mesh Actors and Components
     * - Light Actors and Components
     * - Torus Collider Actors and Torus Components
     * - And any other actor/component that implements GetProperties/SetProperties
     *
     * @param FilePath Json형식으로 World정보가 저장된 파일의 경로
     * @param OutWorld 생성된 World
     */
    static void LoadSceneFromJsonFile(const std::filesystem::path& FilePath, UWorld& OutWorld);

    /**
     * World를 Json형식으로 저장합니다.
     *
     * Saves all actors and components including:
     * - Static Mesh Actors and Components
     * - Light Actors and Components
     * - Torus Collider Actors and Torus Components (with MajorRadius, MinorRadius, TorusAxis)
     * - And any other actor/component that implements GetProperties/SetProperties
     *
     * @param FilePath World를 저장할 파일 경로
     * @param InWorld 저장할 World
     * @return 성공적으로 저장되었는지 여부
     */
    static bool SaveSceneToJsonFile(const std::filesystem::path& FilePath, const UWorld& InWorld);

private:
    /**
     * JSON 문자열을 역직렬화하여 FSceneData를 생성합니다.
     *
     * @param InJsonString 역직렬화할 JSON 문자열
     * @param OutSceneData 역직렬화된 FSceneData 객체
     * @return 성공 여부
     */
    static bool JsonToSceneData(const FString& InJsonString, NS_SceneManagerData::FSceneData& OutSceneData);

    static bool SceneDataToJson(const NS_SceneManagerData::FSceneData& InSceneData, FString& OutJsonString);

    /**
     * World를 FSceneData로 직렬화합니다.
     *
     * @param InWorld 직렬화할 World 객체
     * @return World를 표현하는 FSceneData 객체
     */
    static NS_SceneManagerData::FSceneData WorldToSceneData(const UWorld& InWorld);

    
    static bool LoadWorldFromData(const NS_SceneManagerData::FSceneData& sceneData, UWorld* targetWorld);

private:
    // TODO: IFileManager::Get().CreateFileReader() & Writer() 만들면 여기에 추가
    // static void DeserializeFromBinary(const void* Data, int64 Size, NS_SceneManagerData::FSceneData& OutSceneData);
    // or 중간 구조체 사용 안할경우
    // static UWorld* DeserializeFromBinary(const void* Data, int64 Size);

    // static TArray<uint8> SerializeToBinary(const NS_SceneManagerData::FSceneData& SceneData);
    // or 중간 구조체 사용 안할경우
    // static TArray<uint8> SerializeToBinary(UWorld* World);
};
