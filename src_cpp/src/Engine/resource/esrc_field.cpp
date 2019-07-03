/*
 * ======================= esrc_field.cpp ==========================
 *                          -- tpr --
 *                                        CREATE -- 2019.01.16
 *                                        MODIFY -- 
 * ----------------------------------------------------------
 */
//-------------------- CPP --------------------//
#include <unordered_map>
#include <mutex>
#include <shared_mutex> //- c++17 读写锁
#include <set>


//-------------------- Engine --------------------//
#include "tprAssert.h"
#include "esrc_field.h"
#include "config.h"
#include "chunkKey.h"

#include "esrc_ecoObj.h"
#include "esrc_gameObj.h" 
#include "esrc_chunk.h" 

//-------------------- Script --------------------//
#include "Script/resource/ssrc.h"
#include "Script/gameObjs/create_goes.h"


#include "tprDebug.h"

namespace esrc {//------------------ namespace: esrc -------------------------//

namespace field_inn {//------------ namespace: field_inn --------------//

    std::unordered_map<fieldKey_t,MapField> fields {};
    std::shared_mutex  fieldsSharedMutex; //- 读写锁

    //-- 正在创建的 field 表 --
    std::set<fieldKey_t> fieldsBuilding {};
    std::mutex  fieldsBuildingMutex;

    //===== funcs =====//
    void insert_2_fieldsBuilding( fieldKey_t fieldKey_ );
    bool is_in_fieldsBuilding( fieldKey_t fieldKey_ );
    void erase_from_fieldsBuilding( fieldKey_t fieldKey_ );

    bool is_find_in_fields_( fieldKey_t _key ){
        return (field_inn::fields.find(_key) != field_inn::fields.end());
    }

}//---------------- namespace: field_inn end --------------//


/* ===========================================================
 *      atom_try_to_insert_and_init_the_field_ptr   [-WRITE-]
 * -----------------------------------------------------------
 * 检测是否存在，若不存在，生成之。
 * ----
 * 展示了如何使用 unique_lock 来实现 实例init。
 * ----
 * 目前被 check_and_build_sections_3.cpp -> build_one_chunk_3() 调用
 */
void atom_try_to_insert_and_init_the_field_ptr( const IntVec2 &fieldMPos_ ){

    fieldKey_t fieldKey = fieldMPos_2_fieldKey( fieldMPos_ );
    //--- lock---//
    std::unique_lock<std::shared_mutex> ul( field_inn::fieldsSharedMutex ); //- write -
    if( field_inn::is_find_in_fields_(fieldKey) ||
        ( field_inn::is_in_fieldsBuilding(fieldKey) ) ){
        return;
    }
    field_inn::insert_2_fieldsBuilding( fieldKey );
    
        //--- unlock ---//
        ul.unlock();
        // ***| INIT FIRST, INSERT LATER  |***
        MapField  field {};
        field.init( fieldMPos_ ); 
                //-- 这里耗时有点长, 所以在 解锁状态运行
                //   这样就不会耽误 其他线程 对 全局容器的 访问

    //--- lock ---//
    ul.lock();
        tprAssert( field_inn::is_find_in_fields_(fieldKey) == false ); //- MUST NOT EXIST
    field_inn::fields.insert({ fieldKey, field }); //- copy
    field_inn::erase_from_fieldsBuilding( fieldKey );    
}


/* ===========================================================
 *           atom_field_reflesh_min_and_max_altis     [-WRITE-]
 * -----------------------------------------------------------
 */
void atom_field_reflesh_min_and_max_altis(fieldKey_t fieldKey_, const MapAltitude &alti_ ){
    {//--- atom ---//
        std::unique_lock<std::shared_mutex> ul( field_inn::fieldsSharedMutex ); //- write -
        tprAssert( field_inn::is_find_in_fields_(fieldKey_) ); //- MUST EXIST
        field_inn::fields.at(fieldKey_).reflesh_min_and_max_altis( alti_ );
    }
}



/* ===========================================================
 *           atom_field_set_nodeAlti_2     [-WRITE-]
 * -----------------------------------------------------------
 * -- 仅被 Chunk::init() 使用
 */
void atom_field_set_nodeAlti_2( fieldKey_t fieldKey_, 
                                const std::vector<MemMapEnt> &_chunkMapEnts ){
    {//--- atom ---//
        std::unique_lock<std::shared_mutex> ul( field_inn::fieldsSharedMutex ); //- write -
        tprAssert( field_inn::is_find_in_fields_(fieldKey_) ); //- MUST EXIST
        field_inn::fields.at(fieldKey_).set_nodeAlti_2( _chunkMapEnts );
    }
}


/* ===========================================================
 *       atom_get_mapFieldData_in_chunkBuild     [-READ-]
 * -----------------------------------------------------------
 */
const std::pair<occupyWeight_t, MapFieldData_In_ChunkBuild> atom_get_mapFieldData_in_chunkBuild( fieldKey_t fieldKey_ ){
    std::pair<occupyWeight_t, MapFieldData_In_ChunkBuild> pair {};
    {//--- atom ---//
        std::shared_lock<std::shared_mutex> sl( field_inn::fieldsSharedMutex ); //- read -
            tprAssert( field_inn::is_find_in_fields_(fieldKey_) ); //- MUST EXIST
        const auto &field = field_inn::fields.at( fieldKey_ );
        pair.first = field.get_occupyWeight();
        //---
        pair.second.fieldKey = field.get_fieldKey();
        pair.second.ecoObjKey = field.get_ecoObjKey();
        pair.second.densityIdx = field.get_density().get_idx();
        pair.second.fieldBorderSetId = field.get_fieldBorderSetId();
        pair.second.nodeMPos = field.get_nodeMPos();
    }
    return pair;
}



/* ===========================================================
 *           atom_create_a_go_in_field       [-READ-]
 * -----------------------------------------------------------
 * -1- 根据 field 信息，确定将要生成的 go 类型
 * -2- 计算生成概率
 * -3- 正式执行生成
 */
void atom_create_a_go_in_field( fieldKey_t fieldKey_ ){
    //--- atom ---//
    std::shared_lock<std::shared_mutex> sl( field_inn::fieldsSharedMutex ); //- read -
        tprAssert( field_inn::is_find_in_fields_(fieldKey_) ); //- MUST EXIST
    const MapField &fieldRef = field_inn::fields.at( fieldKey_ );

    sectionKey_t   ecoObjKey = fieldRef.get_ecoObjKey();
    goSpecId_t     goSpecId {};

    float randV = fieldRef.get_weight() * 0.35f + 313.17f; //- 确保大于0
    float fract = randV - floor(randV); //- 小数部分
    tprAssert( (fract>=0.0f) && (fract<=1.0f) );

    //-- 暂时只生成 陆地 go --
    if( fieldRef.is_land() ){
        goSpecId = esrc::atom_ecoObj_apply_a_rand_goSpecId( ecoObjKey,
                                                            fieldRef.get_density().get_idx(),
                                                            fieldRef.get_weight() );

        if( fract <= esrc::atom_ecoObj_get_applyPercent( ecoObjKey, fieldRef.get_density()) ){
            gameObjs::create_a_Go(  goSpecId,
                                    fieldRef.get_nodeMPos(),
                                    fieldRef.get_weight(),
                                    fieldRef.get_nodeMapAlti(), //- tmp 有问题
                                    fieldRef.get_density() );
        }
    }
}


/* ===========================================================    tmp.......
 *              atom_get_field     [-READ-]
 * -----------------------------------------------------------
 *     debug 用.....
 */
const MapField &atom_get_field( fieldKey_t fieldKey_ ){
    //--- atom ---//
    std::shared_lock<std::shared_mutex> sl( field_inn::fieldsSharedMutex ); //- read -
        tprAssert( field_inn::is_find_in_fields_(fieldKey_) ); //- MUST EXIST
    return field_inn::fields.at( fieldKey_ );
}




namespace field_inn {//------------ namespace: field_inn --------------//


/* ===========================================================
 *              fieldsBuilding funcs
 * -----------------------------------------------------------
 */
void insert_2_fieldsBuilding( fieldKey_t fieldKey_ ){
    {//--- atom ---//
        std::lock_guard<std::mutex> lg( fieldsBuildingMutex );
            tprAssert( fieldsBuilding.find(fieldKey_) == fieldsBuilding.end() );
        fieldsBuilding.insert( fieldKey_ );
    }
}
bool is_in_fieldsBuilding( fieldKey_t fieldKey_ ){
    bool ret {};
    {//--- atom ---//
        std::lock_guard<std::mutex> lg( fieldsBuildingMutex );
        ret = fieldsBuilding.find(fieldKey_) != fieldsBuilding.end();
    }
    return ret;
}
void erase_from_fieldsBuilding( fieldKey_t fieldKey_ ){
    {//--- atom ---//
        std::lock_guard<std::mutex> lg( fieldsBuildingMutex );
        tprAssert( fieldsBuilding.erase( fieldKey_ ) == 1 );
    }
}




}//---------------- namespace: field_inn end --------------//
}//---------------------- namespace: esrc end -------------------------//
