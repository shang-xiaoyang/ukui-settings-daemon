#!/bin/bash
  
ts_list=(`ls daemon/res/i18n/*.ts`)
kds_list=(`ls plugins/kds/res/*.ts`)
source /etc/os-release
version=(`echo $ID`)

for kds in "${kds_list[@]}"
do
if [ "$version" == "fedora" ] || [ "$version" == "opensuse-tumbleweed" ] || [ "$version" == "opensuse-leap" ];then
        lrelease-qt5 "${kds}"
    else
    lrelease "${kds}"
fi
done

for ts in "${ts_list[@]}"
do
    printf "\nprocess ${ts}\n"
    if [ "$version" == "fedora" ] || [ "$version" == "opensuse-tumbleweed" ] || [ "$version" == "opensuse-leap" ];then
        lrelease-qt5 "${ts}"
    else
    lrelease "${ts}"
    fi
done
