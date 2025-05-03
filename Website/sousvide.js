
const loglevel = 4;

function convertTempToPerc(val){
    return val < 50 ? 0 : val > 80 ? 100 : (val-50)*10/3;
}
function updateAimSlider(val) {
    $("#thermometer").css("--aim",convertTempToPerc(val));
    $("#aim_text").html(val+"°");
}
function setTemperature(val){
    $("#thermometer").css("--temp",convertTempToPerc(val));
    $("#temp_text").html(val+"°");
}
function request(subadress, data = undefined, callback= (data) => void 0, fail=() => void 0, data_type="json"){
    $.post('http://sousvide.local/'+subadress, data, function(data)
    {
        if(loglevel >=2) console.log("Post Request to "+subadress+" Successful")
        if(loglevel >=4) console.log(data);
        callback(data);
    }
    ,data_type).fail(function(xhr, textStatus, errorThrown) 
    {
        if(loglevel >=2) console.log("Post Request to "+subadress+" Failed")
        if(loglevel >=4) console.log(xhr.responseText + "\n" + textStatus + "\n" + errorThrown);
        fail();
    });
}

let settings = {
    temp: 0,
    running: false,
    aim: 65,
    dur: 0,
    rem_dur: 0,
    start: null,
    end: null
}

function updateInput(obj, str, callback = () => void 0)
{
    if (obj.val() != str && !obj.is(":focus"))
    {
        obj.val(str);
        callback();
    } 
}

function updateTime(obj,time){
    let str = '';
    if(time != null) 
    {
        str = time.getHours().toString().padStart(2,'0') +":" +time.getMinutes().toString().padStart(2,'0');
    }
    updateInput(obj,str);
}
function updateFields(){
    if (settings.running)
    {
        settings.start = new Date(Date.now() + settings.rem_dur - settings.dur);
        settings.end = new Date(Date.now() + settings.rem_dur);
    }
    updateTime($("#start_time"),settings.start);
    updateTime($("#end_time"),settings.end);
    updateInput($("#duration"), Math.ceil(settings.dur/60000).toString());
    updateInput($("#rem_duration"), Math.ceil(settings.rem_dur/60000).toString());
    updateInput($("#aim"), settings.aim, () => {updateAimSlider(settings.aim);});
    setTemperature(settings.temp);
}

function onStart()
{
    $("#startStop").val("Stop!");
    $("#start_time, #end_time").prop('disabled', true);
}

function onStop()
{
    $("#startStop").val("Start!");
    $("#start_time, #end_time").prop('disabled', false);
}

function getInfo(){
    request("getInfo",undefined,function(data){
        settings.temp = data.temp;
        if (settings.running && !data.running)
        {
            onStop();   
        }
        else if (!settings.running && data.running)
        {
            onStart();   
        }
        settings.running = data.running;
        if (settings.running)
        {
            settings.aim = data.aim;
            settings.dur = data.dur;
            settings.rem_dur = data.rem_dur;
        }
        updateFields();
    });
}

function startStop(){
    if(!settings.running)
    {
        let diff = 0;
        if (settings.start) 
        {
            let datenow = new Date();
            settings.start = new Date(datenow.getFullYear(),datenow.getMonth(),datenow.getDate(),settings.start.getHours(),settings.start.getMinutes());
            if (datenow.getTime() > settings.start.getTime()) 
                settings.start.setDate(settings.start.getDate()+1);
            diff = settings.start.getTime()-datenow.getTime();
        }
        request("start",{start: diff, dur: settings.dur, aim: settings.aim});

    }
    else 
    {    
        request("stop");
    }

}
function setAim(){
    request("setAim",{aim: settings.aim});
}
function setDur(){
    request("setDur",{dur: settings.dur});
}

function onChangeAim(val){
    settings.aim = val;
    if(settings.running)
    {
        setAim();
    }
}

function onChangeStart(val){
    console.assert(!settings.running, "changing start while system is running should not be possible");
    if (val=="")
    {
        settings.start = null;
        settings.end = null;
    }
    else{
        let [h,m] = val.split(":").map(elem=> parseInt(elem));
        settings.start = new Date(0,0,0, h, m, 0, 0); 
        settings.end = new Date(settings.start.valueOf() + settings.dur);
    }
    updateTime($("#end_time"),settings.end);
}

function onChangeEnd(val){
    console.assert(!settings.running, "changing end while system is running should not be possible");
    if (val=="")
    {
        settings.start = null;
        settings.end = null;
    }
    else
    {
        let [h,m] = val.split(":").map(elem=> parseInt(elem));
        settings.end = new Date(0,0,0, h, m, 0, 0); 
        settings.start = new Date(settings.end.valueOf() - settings.dur);
    }
    updateTime($("#start_time"),settings.start);
}
function onChangeDur(val){
    settings.dur = parseInt('0'+val) * 60000;
    if(!settings.running)
    {
        if(settings.start != null)
        {
            settings.end = new Date(settings.start.getTime() + settings.dur);
            updateTime($("#end_time"),settings.end);
        }
    }
    else{
        console.log(settings.dur);
        setDur();
    }
}

function update(){
    getInfo()
    setTimeout(update, 500);
}

$(document).ready(function(){
    updateAimSlider(settings.aim)
    update();
});