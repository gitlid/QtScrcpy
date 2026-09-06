// QtScrcpy adapter for pinned ScrcpyKeyMapper. Node rendering stays upstream.
import { App } from './js/app.js';
import { KeyInputManager } from './js/managers/KeyInputManager.js';
import { ConfigManager } from './js/managers/ConfigManager.js';
const clone=v=>JSON.parse(JSON.stringify(v));
const buttons=['LeftButton','MiddleButton','RightButton','BackButton','ForwardButton'];
const aliases={XButton1:'BackButton',ExtraButton1:'BackButton',XButton2:'ForwardButton',ExtraButton2:'ForwardButton'};
const supported=new Set(['KMT_CLICK','KMT_CLICK_TWICE','KMT_CLICK_MULTI','KMT_STEER_WHEEL','KMT_DRAG','KMT_MOUSE_MOVE']);
let host=null,focused=null,initial={},opaque=[],loading=false;
const callbacks=new WeakMap();
function status(s){document.getElementById('integrationStatus').textContent=s;}
function changed(){if(!loading&&host)host.changed();}
KeyInputManager.prototype.setupKeyListener=function(input,callback){
    if(!input||callbacks.has(input))return;
    callbacks.set(input,callback);input.readOnly=true;
    input.addEventListener('focus',()=>{focused=input;input.classList.add('listening');});
    input.addEventListener('blur',()=>input.classList.remove('listening'));
    const assign=key=>{if(!key)return;callback(aliases[key]||key);changed();};
    input.addEventListener('keydown',e=>{e.preventDefault();e.stopPropagation();if(!e.repeat)assign(this.getQtKey(e.code,e.key));});
    input.addEventListener('keyup',e=>{e.preventDefault();e.stopPropagation();});
    input.addEventListener('mousedown',e=>{if(e.button===0)return;e.preventDefault();e.stopPropagation();input.focus();assign(buttons[e.button]);});
    input.addEventListener('contextmenu',e=>e.preventDefault());
};
// File operations belong to native dialogs, never webpage downloads.
ConfigManager.prototype.setupConfigButtons=function(){};
localStorage.setItem('language','zh');
const app=new App();window.qscApp=app;
configManager.switchKey='Key_QuoteLeft';
const bar=document.createElement('div');bar.className='integration-bar';
bar.innerHTML='<label><input id="mouseLookEnabled" type="checkbox"> 鼠标视角：隐藏鼠标并将移动转换为拖动</label><span id="integrationStatus"></span><div id="mouseBindings">绑定到当前按键框：</div>';
document.querySelector('main').before(bar);
const look=document.getElementById('mouseLookEnabled');look.addEventListener('change',changed);
for(const [i,label] of ['左键','中键','右键','侧键 X1','侧键 X2'].entries()){
    const b=document.createElement('button');b.type='button';b.className='btn btn-sm btn-outline-secondary';b.textContent=label;b.dataset.mouse=buttons[i];
    b.addEventListener('mousedown',e=>e.preventDefault());
    b.addEventListener('click',()=>{if(focused&&focused.isConnected&&!focused.disabled&&callbacks.has(focused)){callbacks.get(focused)(buttons[i]);changed();}else status('请先选择一个控件，并点击它的按键输入框。');});
    document.getElementById('mouseBindings').append(b);
}
for(const id of ['saveConfig','loadConfig','uploadBackground']){const el=document.getElementById(id);if(el)el.closest('label')?.remove();}
document.querySelector('.footer a')?.removeAttribute('href');
scaleManager.handleResize(app.stage.container());
function importConfig(data){
    if(!data||typeof data!=='object'||!Array.isArray(data.keyMapNodes)||data.keyMapNodes.length>500)throw Error('无效的映射配置');
    const old=window.qscEditor?exportConfig():null;
    function apply(value){
        loading=true;nodeManager.clearAllNodes();initial=clone(value);opaque=[];
        configManager.switchKey=value.switchKey||'Key_QuoteLeft';look.checked=value.mouseLookEnabled===true;
        if(value.mouseMoveMap){
            const m=clone(value.mouseMoveMap);m.type='KMT_MOUSE_MOVE';
            if(m.speedRatio!==undefined){m.speedRatioX=m.speedRatioX??m.speedRatio;m.speedRatioY=m.speedRatioY??m.speedRatio/2.25;}
            nodeManager.createNode(m);
        }
        for(const source of value.keyMapNodes){
            if(!supported.has(source.type)){opaque.push(clone(source));continue;}
            nodeManager.createNode(clone(source));
        }
        nodeManager.layer.draw();loading=false;
        status(opaque.length?`${opaque.length} 个高级节点不支持可视化编辑，原字段会保留。`:value.mouseMoveMap&&!look.checked?'视角参数已保留但没有启用；需要 FPS 视角时请明确勾选。':'全部键位可同时查看和拖动。编辑不会向手机发送输入。');
    }
    try{apply(data);}catch(e){if(old)apply(old);loading=false;throw e;}
}
function exportConfig(){
    const result=clone(initial),nodes=nodeManager.getMappingsData().map(clone);
    const views=nodes.filter(n=>n.type==='KMT_MOUSE_MOVE');
    if(views.length>1)throw Error('只能保留一个鼠标视角节点');
    result.switchKey=configManager.switchKey;result.mouseLookEnabled=look.checked;
    result.keyMapNodes=[...nodes.filter(n=>n.type!=='KMT_MOUSE_MOVE'),...clone(opaque)];
    if(views.length){
        const m=views[0];
        if(m.smallEyes?.enabled)m.smallEyes={...(initial.mouseMoveMap?.smallEyes||{}),...m.smallEyes};
        delete m.speedRatio;
        if(!m.smallEyes?.enabled)delete m.smallEyes;
        result.mouseMoveMap=m;
    }else{delete result.mouseMoveMap;}
    return result;
}
async function setBackground(data){
    if(!/^data:image\/png;base64,/.test(data))throw Error('背景必须是 PNG');
    const image=new Image();await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=reject;image.src=data;});
    app.layer.find('.background').forEach(n=>n.destroy());
    const s=Math.min(app.stage.width()/image.width,app.stage.height()/image.height);
    const background=new Konva.Image({image,width:image.width*s,height:image.height*s,x:(app.stage.width()-image.width*s)/2,y:(app.stage.height()-image.height*s)/2,name:'background',listening:false});
    background.setAttr('originalWidth',image.width);background.setAttr('originalHeight',image.height);
    app.layer.add(background);background.moveToBottom();app.layer.draw();
}
window.qscEditor={importConfig,exportConfig,setBackground,ready:false};
document.addEventListener('mousedown',e=>{if(e.button>=3)e.preventDefault();if(e.target.closest('#canvasContainer'))changed();},true);
document.addEventListener('keydown',e=>{if(!e.repeat)changed();},true);
document.addEventListener('input',changed);document.addEventListener('change',changed);
document.addEventListener('click',e=>{if(e.target.closest('.mapping-type-item,#deleteMapping,#clearAllNodes,#moveNodeBack,#moveNodeFront'))changed();});
app.stage.on('dragend',changed);
const createNode=nodeManager.createNode.bind(nodeManager);
nodeManager.createNode=function(source){if(this.nodes.length>=500){status('节点数量上限为 500。');return null;}const node=createNode(source);if(node)changed();return node;};
const clearAll=nodeManager.clearAllNodes.bind(nodeManager);
nodeManager.clearAllNodes=function(){clearAll();if(!loading){opaque=[];changed();}};
if(window.qt&&window.QWebChannel){
    new QWebChannel(qt.webChannelTransport,channel=>{
        host=channel.objects.host;
        host.bootstrap(async payload=>{try{
            await setBackground(payload.background);importConfig(JSON.parse(payload.config));
            window.qscEditor.ready=true;host.editorReady();
        }catch(e){status(String(e));host.editorFailed(String(e));}});
    });
}else{window.qscEditor.ready=true;status('独立浏览器测试：没有连接手机。');}
