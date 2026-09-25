"""为 DT_WeaponData 写入四把正式武器的独立弹匣 StaticMesh 引用。"""

import unreal
import csv
from pathlib import Path


TABLE_PATH = "/Game/Shooter/Data/DT_WeaponData"
CSV_PATH = Path(unreal.Paths.project_dir()) / "Saved/WeaponMagazines/DT_WeaponData.csv"
MAGAZINE_MESHES = {
    "Rifle": "/Game/Weapons/Rifle/Meshes/SM_Rifle_Magazine",
    "Pistol": "/Game/Weapons/Pistol/Meshes/SM_Pistol_Magazine",
    "AWP": "/Game/Weapons/AWP/Meshes/SM_AWP_Magazine",
    "GrenadeLauncher": "/Game/Weapons/GrenadeLauncher/Meshes/SM_GrenadeLauncher_Magazine",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    table = unreal.load_asset(TABLE_PATH)
    require(table is not None, "武器配置表加载失败: " + TABLE_PATH)
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    require(
        unreal.DataTableFunctionLibrary.export_data_table_to_csv_file(table, str(CSV_PATH)),
        "武器配置表导出失败")

    with CSV_PATH.open("r", encoding="utf-8", newline="") as stream:
        rows = list(csv.reader(stream))
    require(rows and rows[0], "武器配置表 CSV 为空")
    magazine_column = rows[0].index("MagazineMesh")
    row_names = {row[0]: row for row in rows[1:]}
    for row_name, mesh_path in MAGAZINE_MESHES.items():
        require(row_name in row_names, "武器配置行缺失: " + row_name)
        mesh = unreal.load_asset(mesh_path)
        require(mesh is not None, "独立弹匣加载失败: " + mesh_path)
        mesh_name = mesh_path.rsplit("/", 1)[-1]
        row_names[row_name][magazine_column] = mesh_path + "." + mesh_name

    with CSV_PATH.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream, lineterminator="\n").writerows(rows)

    require(
        unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(CSV_PATH)),
        "武器配置表 CSV 回写失败")
    require(unreal.EditorAssetLibrary.save_asset(TABLE_PATH), "武器配置表保存失败")
    unreal.log("已写入四把武器的 MagazineMesh 配置")


main()
