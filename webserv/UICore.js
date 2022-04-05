"use strict";

// =======================================================================
// =======================================================================
// constructor data structure
// UIBsae :
// {
//      id: id of this element, (!!! this parameter needs to be first)
//      create_type: if no pre-defined element exist, the constructor can create an element by itself, specify the type
//          for example: div, p, h1, button, img ....  (!!! if this parameter is required, please place it in second order)
//      name: name of this element
//      parent: parent
//      rect: { x, y, width, height }
//      ev_render: event of render function
//      ev_reload: event of reload function
//      ev_change: event of onchange
//      ev_click: event of onclick
//      ev_keypress: event of onkeypress
//      ev_keydown: event of onkeydown
//      ev_blur: event of onblur
// }
class UIBase {

    constructor(obj) {

        // 元件的style
        this.sStyle="";
        // 元件的class
        this.sClass="";
        if (obj==undefined)
            return;
        // 元件的DOM id
        if (obj.id) {
            this.oSelf=document.getElementById(obj.id);
            // UIPage不用檢查id
            if (!this.oSelf) {
                console.log(`Warning: id( ${obj.id} ) not found for ${this.constructor.name}!`);
            }
            this.sId=obj.id;
            UIBase.insObj(this);
        }
        else
            this.sId="";
        // 元件的name
        if (obj.name)
            this.sName=obj.name;
        else
            this.sName="";
        // 元件的parent, 以object方式指定(意義上就是by reference)
        if (obj.parent)
            this.oParent=obj.parent;
        else
            this.oParent=null;
        // render event
        if (obj.ev_render)
            this.ev_render=obj.ev_render;
        else
            this.ev_render=null;
        // set value event
        if (obj.ev_setValue)
            this.ev_setValue=obj.ev_setValue;
        else
            this.ev_setValue=null;

        // set OnChange event
        if (obj.ev_change) {
            this.setOnChange(obj.ev_change);
        }

        // set OnClick event
        if (obj.ev_click) {
            this.setOnClick(obj.ev_click);
        }

        // set OnKeyPress event
        if (obj.ev_keypress) {
            this.setOnKeyPress(obj.ev_keypress);
        }

        // set OnKeyDown event
        if (obj.ev_keydown) {
            this.setOnKeyDown(obj.ev_keydown);
        }

        // set OnBlur event
        if (obj.ev_blur) {
            this.setOnBlur(obj.ev_blur);
        }

        if (obj.rect) {
            this.setRect(obj.rect);
        }
    }

    // ======================================================================
    // ======================================================================
    render() {
        if (this.ev_reload)
            this.ev_reload();
    }
    // ======================================================================
    setVisible(v) {
        let obj = this.oSelf;
        if (v===false)
            obj.style.display = "none";
        else if (v===true)
            obj.style.display = "block";
        else if (typeof v === 'string' || v instanceof String) {
            obj.style.display = v;
        }
        else {
            console.error("setVisible: Invalid parameter! ", v, "  ",  new Error().stack);
        }
    }
    getVisible() {
        if (this.oSelf.style.display=="none")
            return false;
        else
            return true;
    }
    // ======================================================================
    setOnChange(ev) {
        this.oSelf.onchange = ev;
    }
    // ======================================================================
    setOnClick(ev) {
        this.oSelf.onclick = ev;
    }
    // ======================================================================
    setOnKeyPress(ev) {
        this.oSelf.onkeypress = ev;
    }
    // ======================================================================
    setOnKeyDown(ev) {
        this.oSelf.onkeydown = ev;
    }
    // ======================================================================
    setOnBlur(ev) {
        this.oSelf.onblur = ev;
    }
    // ======================================================================
    // rect : {x, y, width, height}
    setRect(rect) {
        if ("x" in rect && "y" in rect && "width" in rect && "height" in rect) {
            this.oSelf.style.marginLeft = rect.x;
            this.oSelf.style.marginTop = rect.y;
            this.oSelf.style.width = rect.width;
            this.oSelf.style.height = rect.height;
        }
        else {
            console.error(`Bad rect parameters (id={$this->sId}) : `, rect);
        }
    }
    getValue() {
        return this.oSelf.value;
    }
    setValue(v) {
        this.oSelf.value = v;
    }
    getText() {
        return this.oSelf.innerText;
    }
    setText(v) {
        this.oSelf.innerText = v;
    }

    set Visible(v) {
        this.setVisible(v);
    }

    get Visible() {
        return this.getVisible();
    }

    set Value(v) {
        this.setValue(v);
    }

    get Value() {
        return this.getValue();
    }

    set Text(v) {
        this.setText(v);
    }

    get Text() {
        return this.getText();
    }
    // 找出相對應id的UIBase系列物件
    static Obj(idname) {
        for(let i=0;i<UIBase.lstInstances.length;i++) {
            if (UIBase.lstInstances[i].sId==idname)
                return UIBase.lstInstances[i];
            else false;
        }
    }
    static insObj(o) {
        UIBase.lstInstances.push(o);
    }

    // 取得序號
    static getSeq() {
        let ms = Date.now() % 1000;
        let ret = UIBase.seqNum * 1000 + ms;
        UIBase.seqNum++;
        return ret;
    }

    dup(idname) {
        let obj = new this.constructor;
        Object.assign(obj, this);
        if (obj.oParent)
            obj.oParent.addControl(obj);
        // dulicate DOM, assign the id with idname
        let newdom = obj.oSelf.cloneNode(true);
        if (idname) {
            newdom.id=idname;
            obj.sId=idname;
        }
        else {
            newdom.id=newdom.id+"_dup";
            obj.sId=newdom.id+"_dup";
        }
        UIBase.insObj(obj);
        obj.oSelf = newdom;
        this.oSelf.parentNode.appendChild(newdom);
        //obj.oParent.addControl(obj);
    }

    kill() {
        // remove the instance from list
        if (this.oParent) {
            let ndx = this.oParent.lstControls.findIndex((x) => x==this);
            this.oParent.lstControls.splice(ndx, 1);
        }
        this.oSelf.remove();
        let ndxself = UIBase.lstInstances.findIndex((x) => x==this);
        UIBase.lstInstances.splice(ndxself,1);
    }
}

// set static property
// all new instance with id can be added into this list
UIBase.lstInstances=[];
UIBase.seqNum=0;

// =======================================================================
// =======================================================================
class UIContainer extends UIBase {

    constructor(obj) {
        super(obj);
        // 元件的子孫
        this.lstControls=[];
    }
    // ======================================================================

    // ======================================================================
    getControl(id) {
    }

    // ======================================================================
    addControl(c) {
        if (!(c instanceof UIBase)) {
            console.error("addControl: Invalid UIBase Type!",  new Error().stack);
            return;
        }
        c.oParent=this;
        this.lstControls.push(c);
    }

}

// =======================================================================
// ready : function() {}
//
class UIPage extends UIContainer {
    constructor(obj) {
        super(obj);
        this.oSelf = document;
        if (obj==undefined)
            return;
        if (obj.ready) {
            this.setOnReady(obj.ready);
        }
    }
    setOnReady(ev) {
        document.addEventListener("DOMContentLoaded", ev);
    }
    redirect(url) {
        window.location.replace(url);
    }
    reload() {
        location.reload();
    }
}
// =======================================================================
// items: [
//      {
//          value: "aaaa",
//          text: "bbbb"
//      }
// ]
// =======================================================================
class UICombobox extends UIBase {
    constructor(obj) {
        super(obj);
        if (obj==undefined)
            return;
        if (obj.items)
            this.setItems(obj.items);
    }

    clearItems() {
        while (this.oSelf.options.length>0)
            this.oSelf.remove(0);
    }

    // send an array of items to setItems()
    //[
    //      {
    //          value: "aaaa",
    //          text: "bbbb"
    //      }
    //]
    setItems(itm) {
        this.clearItems();
        for(let i=0;i<itm.length;i++) {
            var option = document.createElement('option');
            option.text = itm[i].text;
            option.value = itm[i].value;
            this.oSelf.add(option);
        }
    }

    getIndex() {
        return this.oSelf.selectedIndex;
    }

    setIndex(v) {
        this.oSelf.selectedIndex=v;
    }

    getText() {
        return this.oSelf.options[this.oSelf.selectedIndex].text;
    }

    getValue() {
        return this.oSelf.options[this.oSelf.selectedIndex].value;
    }

    get Value() {
        return this.getValue();
    }

    get Text() {
        return this.getText();
    }

    get Index() {
        return this.getIndex();
    }

    set Index(v) {
        return this.setIndex(v);
    }
    selectByValue(v) {
        let x = this.oSelf;
        if (!x.options.length) {
            x.selectedIndex = -1;
        } else {
            x.selectedIndex = 0;
        }
        for (let i = 0; i < x.options.length; i++) {
            if (x.options[i].value == v) {
                x.selectedIndex = i;
                break;
            }
        }
    }

    selectByText(v) {
        let x = this.oSelf;
        if (!x.options.length) {
            x.selectedIndex = -1;
        } else {
            x.selectedIndex = 0;
        }
        for (let i = 0; i < x.options.length; i++) {
            if (x.options[i].text == v) {
                x.selectedIndex = i;
                break;
            }
        }
    }


    // API回傳一個array, 是 name / value pair, 分別指定API回傳的value及name的key名稱
    initComboListFromAPI(apiurl, text_name="text", value_name="value") {
        return new Promise(async (resolve, reject) => {
            this.clearItems();
            let succ=false;
            let fm = new UIForm();
            let ret = await fm.post(apiurl);
            if (ret && ret.status==200) {
                succ=true;
            }
            else {
                // post不行改用get
                ret = await fm.get(apiurl);
                if (ret && ret.status==200) {
                    succ=true;
                }
                else {
                    resolve(false);
                }
            }
            if (succ) {
                var retdata = JSON.parse(ret.data);  //把一個JSON字串轉換成 JavaScript的數值或是物件
                for (let i = 0; i < retdata.length; i++) {
                    var x = this.oSelf;
                    var option = document.createElement("option");
                    option.text = retdata[i][text_name];
                    option.value = retdata[i][value_name];
                    x.add(option);
                }
                resolve(true);
            }
            else
                resolve(false);
        });
    }
    
}
// =======================================================================
// send a form data, the data should be added to perform post() or get()
// use addControl() to add key/value pair
//
//  header : {
//      key1: value1,
//      key2: value2,...
//  }
//  extraData : {
//      key1: value1,
//      key2: value2,...
//  }
//  timeout: number in milliseconds
//  ev_timeout : event function to handle timeout, function(e)
//  ev_response : event function to handle response, function(status, data)
//
class UIForm extends UIContainer {
    constructor(obj) {
        super(obj);
        this.sExtraData=[];
        this.sHeader=[];
        if (obj==undefined)
            return;
        if (obj.extraData)
            this.setExtraData(obj.extraData);
        if (obj.header)
            this.setHeader(obj.header);
        if (obj.timeout)
            this.timeout = obj.timeout;
        if (obj.ev_timeout)
            this.setTimeout(obj.ev_timeout);
        if (obj.ev_response)
            this.setResponse(obj.ev_response);

    }

    setTimeout(ev) {
        this.ev_timeout = ev;
    }

    setResponse(ev) {
        this.ev_response = ev;
    }
    setHeader(hdr) {
        this.sHeader={};
        if (Object.values(hdr).length>0)
            this.sHeader=hdr;
    }
    setExtraData(extraData) {
        this.extraData={};
        if (Object.values(extraData).length>0)
            this.sExtraData = extraData;
    }

    post(url) {
        return new Promise((resolve, reject) => {
            let wholeJson = { };
            let self = this;

            if (this.lstControls)
                for (let i=0;i<this.lstControls.length;i++) {
                    if (this.lstControls[i].oSelf.name && this.lstControls[i].oSelf.name !="")
                        wholeJson[this.lstControls[i].oSelf.name] = this.lstControls[i].oSelf.value;
                    else
                        wholeJson[this.lstControls[i].oSelf.id] = this.lstControls[i].oSelf.value;
                }
            if (this.sExtraData)
                for (const key in this.sExtraData) {
                    wholeJson[key] = this.sExtraData[key];
                }
            var xmlhttp = new XMLHttpRequest();
            // event to receive response
            xmlhttp.onreadystatechange = function () {
                if (xmlhttp.readyState == 4 ) {
                    if (xmlhttp.status == 200) {
                        resolve({status: xmlhttp.status, data: this.response});
                        if (self.ev_response)
                            self.ev_response(xmlhttp.status, this.response);
                    }
                    else {
                        resolve({status: xmlhttp.status, data: ""});
                        if (self.ev_response)
                            self.ev_response(xmlhttp.status, "");
                    }
                }
            }
            xmlhttp.open("POST", url, true);
            xmlhttp.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
            if (this.sHeader)
                for(const key in this.sHeader) {
                    xmlhttp.setRequestHeader(key, this.sHeader[key]);
                }
            if (this.timeout)
                xmlhttp.timeout = this.timeout;
            else
                xmlhttp.timeout = 5000;   // 5 seconds by default
            // event to receive timeout
            xmlhttp.ontimeout = function (e) {
                self.ev_timeout(e);
                resolve({status: 408, data:"timeout"});
            };
            xmlhttp.send(JSON.stringify(wholeJson));
        });
    }

    get(url) {
        return new Promise((resolve, reject) => {
            let wholeJson = { };
            let self = this;

            if (this.lstControls)
                for (let i=0;i<this.lstControls.length;i++) {
                    if (this.lstControls[i].oSelf.name && this.lstControls[i].oSelf.name !="")
                        wholeJson[this.lstControls[i].oSelf.name] = this.lstControls[i].oSelf.value;
                    else
                        wholeJson[this.lstControls[i].oSelf.id] = this.lstControls[i].oSelf.value;
                }
            if (this.sExtraData)
                for (const key in this.sExtraData) {
                    wholeJson[key] = this.sExtraData[key];
                }
            var xmlhttp = new XMLHttpRequest();
            // event to receive response
            xmlhttp.onreadystatechange = function () {
                if (xmlhttp.readyState == 4 ) {
                    if (xmlhttp.status == 200) {
                        resolve({status: xmlhttp.status, data: this.response});
                        if (self.ev_response)
                            self.ev_response(xmlhttp.status, this.response);
                    }
                    else {
                        resolve({status: xmlhttp.status, data: ""});
                        if (self.ev_response)
                            self.ev_response(xmlhttp.status, "");
                    }
                }
            }
            xmlhttp.open("GET", url, true);
            xmlhttp.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
            if (this.sHeader)
                for(const key in this.sHeader) {
                    xmlhttp.setRequestHeader(key, this.sHeader[key]);
                }
            if (this.timeout)
                xmlhttp.timeout = this.timeout;
            else
                xmlhttp.timeout = 5000;   // 5 seconds by default
            // event to receive timeout
            xmlhttp.ontimeout = function (e) {
                self.ev_timeout(e);
                resolve({status: 408, data:"timeout"});
            };
            xmlhttp.send(JSON.stringify(wholeJson));
        });
    }

}


// =======================================================================
// allow (escaped) pure text
// and HTML code
class UILabel extends UIBase {
    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="P" && this.oSelf.tagName!="SPAN" && this.oSelf.tagName!="DIV")
            console.error("UILable must be <P>, <SPAN> or <DIV> : id = ", this.oSelf.id);
    }
    // set pure text, escape the content
    setText(s) {
        this.oSelf.innerText = s;
    }
    getText() {
        return this.oSelf.innerText;
    }
    get Text() {
        return this.getText();
    }
    set Text(v) {
        this.setText(v);
    }

    // set raw data, be aware the hacker code!!
    setHTML(s) {
        this.oSelf.innerHTML = s;
    }
    // get innerHTML
    getHTML() {
        return this.oSelf.innerHTML;
    }
    get HTML() {
        return this.getHTML();
    }
    set HTML(v) {
        this.setHTML(v)
    }
}

class UIImage extends UIBase {
}

class UIInputText extends UIBase {
}

class UIFileUpload extends UIBase {
}

class UITextArea extends UIBase {
}

// =======================================================================
// radio button, 同一個group可以使用相同的name, 可以透過getValueByGroup取得group name下所選定的Value
class UIRadiobutton extends UIBase {
    constructor(obj) {
        super(obj);
    }
    getChecked() {
        return this.oSelf.checked;
    }
    setChecked(s) {
        if (s!==true && s!==false) {
            console.error("setChecked must be boolean : id = ", this.oSelf.id);
            return;
        }
        this.oSelf.checked=s;
    }
    set Checked(s) {
        this.setChecked(s);
    }
    get Checked() {
        return this.getChecked();
    }
    getValueByGroup(n) {
        return document.querySelector(`input[name="${n}"]:checked`).value;
    }
}
// =======================================================================
class UICheckbox extends UIBase {
    constructor(obj) {
        super(obj);
    }
    getChecked() {
        return this.oSelf.checked;
    }
    setChecked(s) {
        if (s!==true && s!==false) {
            console.error("setChecked must be boolean : id = ", this.oSelf.id);
            return;
        }
        this.oSelf.checked=s;
    }
    set Checked(s) {
        this.setChecked(s);
    }
    get Checked() {
        return this.getChecked();
    }
}


// =======================================================================
class UIAnchor extends UIBase {
    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="A")
            console.error("UIAnchor must be <A> : id = ", this.oSelf.id);
    }
    // set pure text, escape the content
    setText(s) {
        this.oSelf.innerText = s;
    }
    getText() {
        return this.oSelf.innerText;
    }
    get Text() {
        return this.getText();
    }
    set Text(v) {
        this.setText(v);
    }

    // set raw data, be aware the hacker code!!
    setHTML(s) {
        this.oSelf.innerHTML = s;
    }
    // get innerHTML
    getHTML() {
        return this.oSelf.innerHTML;
    }
    get HTML() {
        return this.getHTML();
    }
    set HTML(v) {
        this.setHTML(v)
    }

    setHref(v) {
        this.oSelf.href = v;
    }
    getHref() {
        return this.oSelf.href;
    }
    set Href(v) {
        this.setHref(v);
    }
    get Href() {
        return this.getHref();
    }
}


// =======================================================================
// columns: ["col_name1", "col_name2", "col_name3"....]
// data: 
//     [ [ "r1_col1_value", "r1_col2_value", "r1_col3_value" ],
//       [ "r2_col1_value", "r2_col2_value", "r2_col3_value" ], ...
//     ],
// widths: [wid_col1, wid_col2, wid_col3......]
//
class UIGrid extends UIBase {

    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="TABLE") {
            console.error("UIGrid must be <TABLE> : id = ", this.oSelf.id);
            return;
        }
        let table = this.oSelf;
        if (obj==undefined)
            return;
        if ("columns" in obj && obj.columns.length>0) {
            this.generateTableHead(obj.columns, obj.widths);
        }
        if ("data" in obj && obj.data.length>0) {
            this.generateTable(obj.data);
        }
    }

    generateTable(data) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        tbody.style.fontWeight="normal";
        // 刪除全部data row
        while (tbody.rows.length>0)
            tbody.deleteRow(0);
        for (let i=0;i<data.length;i++) {
            let row = tbody.insertRow();
            for (let j=0;j<data[i].length;j++) {
                let cell = row.insertCell();
                cell.innerHTML = data[i][j];
            }
        }
    }

    generateTableHead(columns, widths) {
        let thead = this.oSelf.createTHead();
        let row = thead.insertRow();
        for (let i=0;i<columns.length;i++) {
            let th = document.createElement("th");
            //let text = document.createTextNode(columns[i]);
            //th.appendChild(text);
            th.innerHTML = columns[i];
            // 如果有寬度定義, 設定寬度
            if (widths && widths[i]) {
                th.widths = widths[i];
            }
            row.appendChild(th);
        }
        let tbody = this.oSelf.createTBody();
    }

    getCellText(row, col) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        return tbody.rows[row].cells[col].innerText;
    }

    getCellHTML(row, col) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        return tbody.rows[row].cells[col].innerHTML;
    }

    setCellText(row, col, s) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        tbody.rows[row].cells[col].innerText = s;
    }

    setCellHTML(row, col, s) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        tbody.rows[row].cells[col].innerHTML = s;
    }

    // return row number
    findRow(col, search_txt) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[0];
        for(let i=0;i<tbody.rows.length;i++) {
            if (tbody.rows[i].cells[col].innerText==search_txt)
                return i;
        }
        return -1;
    }

    // 依index取得 header row的DOM
    getHeaderDOM(ndx=0) {
        let tbody = this.oSelf.getElementsByTagName('thead')[ndx];
        return tbody;
    }

    // 依index取得 body row的DOM
    getRowDOM(ndx=0) {
        let tbody = this.oSelf.getElementsByTagName('tbody')[ndx];
        return tbody;
    }
}


const BTN_NORMAL =   1;
const BTN_CANCEL =   2;
const BTN_ATTENTION = 4;

// =======================================================================
// create a dialog object
// dialog: id of <div> block
// dlgheader: header string
// buttons: [
//  {
//      type: BTN_NORMAL,
//      text: "ok",
//      btnid: 3    // define button id to identify this button, zero is reserved to the [x] close button on the right-up corner
//  }
// ]
class UIDialog extends UIBase {
    constructor(obj) {
        super(obj);
        // save header string
        if (obj && obj.dlgheader)
            this.dlgHeader = obj.dlgheader;
        else
            this.dlgHeader = "對話框";
        // create modal div
        let div = document.createElement("div");
        div.id = this.sId;
        this.oSelf = div;
        div.classList.add("w3-modal");
        document.body.appendChild(div);
        // create div content
        let div_content = document.createElement("div");
        div_content.classList.add("w3-modal-content","w3-animate-top");
        // add close button
        //let id_close = "btnclose" + (new Date()).getTime();
        div_content.innerHTML=`<header class="w3-container" style="background-color:#e28223"><button data-btn-Id=0 class="w3-button w3-display-topright" onclick="document.getElementById('${this.sId}').style.display='none'">&times;</button><h3>${this.dlgHeader}</h3></header>`;
        div.appendChild(div_content);

        // 將dialog內容加入modal_content內
        if (obj && obj.dialog) {
            let innerdlg = document.getElementById(obj.dialog);
            if (innerdlg) {
                div_content.appendChild(innerdlg);
                // 原本設成隱型的, 再把它設出現
                innerdlg.style.display="block";
            }
            else {
                console.error("UIDialog dialog parameter error!, id = ", this.sId);
                return;
            }
        }

        // add footer
        this.oFooter = document.createElement("footer");
        this.oFooter.classList.add("w3-container", "w3-padding-16");
        div_content.appendChild(this.oFooter);

        // add buttons
        if (obj && obj.buttons) {
            for(let i=0;i<obj.buttons.length;i++) {
                this.addButton(obj.buttons[i].type, obj.buttons[i].text, obj.buttons[i].btnid);
            }
        }
    }

    open() {
        this.oSelf.style.display='block';
    }

    close() {
        this.oSelf.style.display='none';
        // remove button handlers
        let btns = this.oFooter.getElementsByTagName("BUTTON");
        for (let i=0;i<btns.length;i++) {
            btns[i].removeEventListener("click", this.ResolvHandler);
        }
    }

    clearButtons() {
        let btns = this.oFooter.getElementsByTagName("BUTTON");
        for (let i=0;i<btns.length;i++) {
            btns[i].remove();
        }
    }

    addButton(type, text, btnid) {
        let btn = document.createElement("button");
        btn.classList.add("w3-button", "w3-border");
        btn.style.margin="4px";
        btn.innerHTML = text;
        this.oFooter.appendChild(btn);
        if (type==BTN_NORMAL) {
            btn.style.backgroundColor="#02BDE8";
            btn.style.color="#FFFFFF";
        }
        else if (type==BTN_CANCEL) {
            btn.style.backgroundColor="#F2F2F2";
            btn.style.color="#A6A6A6";
        }
        else {
            btn.style.backgroundColor="#FFFFFF";
            btn.style.color="#FF6C28";
        }
        btn.dataset.btnId = btnid;

    }

    openAsync() {
        return new Promise((resolve, reject) => {
            let this_dlg=this;
            this.open();
            // set event of the buttons
            this.ResolvHandler = function() {
                this_dlg.close();
                resolve(this.dataset.btnId);
            }
            let btns = this.oSelf.getElementsByTagName("BUTTON");
            for (let i=0;i<btns.length;i++) {
                // add buttons with custom data btnId
                if (btns[i].dataset && btns[i].dataset.btnId)
                    btns[i].addEventListener("click", this_dlg.ResolvHandler);
            }
        });
    }

}


// =======================================================================
// 專門用來製造Pagebar navigator
class UIPagination extends UIBase {
    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="P" && this.oSelf.tagName!="SPAN" && this.oSelf.tagName!="DIV")
            console.error("UIPagination must be <P>, <SPAN> or <DIV> : id = ", this.oSelf.id);
    }


    // 原本從backend搬到frontend, 這樣比較合理
    // pnow, 是目前的頁數
    // pall, 是全部頁數
    // refresh_func, 是更新list的函式名稱
    // show_select, 是顯示中間頁數的數量
	setPagebar(pnow, pall, refresh_func="RefreshTable", show_select = 3, params="") {
		let firstPg = "第一頁";
		let forPg = "上一頁";
		let lastPg = "最後頁("+pall+")";
		let nextPg = "下一頁";
		let pglist = "";

        if (params!="")
            params = "," + params;

		if (pnow > 1) {
			firstPg = "<li><a style=\"cursor:pointer;\" onclick=\""+ refresh_func +"(1" + params + ")\">" + firstPg + "</a></li>";
			forPg = "<li><a style=\"cursor:pointer;\" onclick=\""+ refresh_func +"(" + (pnow - 1) + params + ")\">" + forPg + "</a></li>";
		} else {
			firstPg = "<li class='nolink'>" + firstPg + "</li>";
			forPg = "<li class='nolink'>" + forPg + "</li>";
		}

		if (pnow < pall) {
			lastPg = "<li><a style=\"cursor:pointer;\" onclick=\""+ refresh_func +"(" + pall + params + ")\">" + lastPg + "</a></li>";
			nextPg = "<li><a style=\"cursor:pointer;\" onclick=\""+ refresh_func +"(" + (pnow + 1) + params + ")\">" + nextPg + "</a></li>";
		} else {
			nextPg = "<li class='nolink'>" + nextPg + "</li>";
			lastPg = "<li class='nolink'>" + lastPg + "</li>";
		}

		let sp = (pnow - show_select < 1) ? 1 : pnow - show_select;
		let op = (pnow + show_select > pall) ? pall : pnow + show_select;

		for (let i = sp; i <= op; i++) {
			if (i == pnow) {
				pglist += `<li class='thispage'>${i}</li>`;
			} else {
				pglist += "<li><a style=\"cursor:pointer;\" onclick=\""+ refresh_func +"(" + i + params + ")\">" + i + "</a></li>";
			}
		}

		let retstr = "<div class='PageBar'><ul>" + firstPg + forPg + pglist + nextPg + lastPg + "</ul></div>";

        this.setHTML(retstr);
		return retstr;
	}
    
    // set pure text, escape the content
    setText(s) {
        this.oSelf.innerText = s;
    }
    // set raw data, be aware the hacker code!!
    setHTML(s) {
        this.oSelf.innerHTML = s;
    }
    // get innerHTML
    getHTML() {
        return this.oSelf.innerHTML;
    }
}

// =======================================================================
// 下拉, 可以選取filter
// 必須為<div>
//
// text : 下拉顯示的文字
// url : API url to get contents, 如果text_name及value_name未指定, 內定使用 value/text
// text_name: API回傳的text名稱
// value_name: API回傳的value名稱
// ev_filterchange: 當filter改變時的事件
// 
class UIDropDownFilter extends UIBase {
    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="DIV")
            console.error("UIDropDownFilter must be <DIV> : id = ", this.oSelf.id);
        // 如果該div沒有包含w3-dropdown-click這個class, 就加上去
        if (!this.oSelf.classList.contains("w3-dropdown-click")) {
            this.oSelf.classList.add("w3-dropdown-click");
        }

        this.text = obj.text;
        // 儲存dropdown list的items
        this.listItems = [];

        this.url = obj.url;
        this.text_name = obj.text_name;
        this.value_name = obj.value_name;

        if (obj.ev_filterchange) {
            this.ev_filterchange = obj.ev_filterchange;
        }
        // content id
        //this.idContent="";

        this.renderHTML();
    }

    static selectAll(id) {
        // 同一個dropdown下面的ck_開頭的checkbox都設定
        let x=document.getElementById(id);
        let p=x.parentNode.parentNode;
        let lst=p.getElementsByTagName("INPUT");
        let modified=false;
        if (x.checked) {
            for(let i=0;i<lst.length;i++) {
                if (lst[i].type=="checkbox" && lst[i].id.substr(0,3)=="ck_" && lst[i].checked==false) {
                    lst[i].checked=true;
                    modified=true;
                }
            }
            let thisobj = UIBase.Obj(p.parentNode.id);
            if (modified && thisobj.ev_filterchange) {
                thisobj.ev_filterchange(thisobj);
            }
        }
    }

    static dropdown_click(id) {
        let x=document.getElementById(id);
        if (x.className.indexOf("w3-show") == -1) { 
            x.className += " w3-show";
        } else {
            x.className = x.className.replace(" w3-show", "");
        }
    }

    static cbClick(id) {
        // 同一個dropdown下面的all_開頭的checkbox, 是全選, 如果有一個解除勾選, 就把它unselect
        let x=document.getElementById(id);
        let p=x.parentNode.parentNode;
        let lst=p.getElementsByTagName("INPUT");
        if (!x.checked) {
            for(let i=0;i<lst.length;i++) {
                if (lst[i].type=="checkbox" && lst[i].id.substr(0,4)=="all_") {
                    lst[i].checked=false;
                }
            }
        }
        let thisobj = UIBase.Obj(p.parentNode.id);
        if (thisobj.ev_filterchange) {
            thisobj.ev_filterchange(thisobj);
        }
        
    }

    // output component's HTML
    async renderHTML() {
        if (this.url) {
            if (this.text_name && this.value_name)
                await this.initDropDownAPI(this.url, this.text_name, this.value_name);
            else
                await this.initDropDownAPI(this.url);
        }

        let id_content = this.sId + "_"+UIBase.getSeq();
        let id_all = "all_"+UIBase.getSeq();
        let itms=`<span class="w3-bar-item w3-button"> <input id="${id_all}" type="checkbox" onclick="UIDropDownFilter.selectAll('${id_all}')" value="-1" checked="checked"> 選擇全部</span>`;
        for (let i=0;i<this.listItems.length;i++) {
            let id_ckbox = "ck_"+UIBase.getSeq();
            itms += `<span class="w3-bar-item w3-button"> <input id="${id_ckbox}" type="checkbox" value="${this.listItems[i].value}" onclick="UIDropDownFilter.cbClick('${id_ckbox}')" checked="checked"> ${this.listItems[i].text}</span>`
        }

        let bdr=`<button style="all: unset" onclick="UIDropDownFilter.dropdown_click('${id_content}')">${this.text} <i class="fa fa-caret-down"></i></button><div id="${id_content}" class="w3-dropdown-content w3-bar-block w3-card-4" style="height: 50vh; overflow-y:auto; min-width:8vw; width: 8vw">${itms}</div>`;
        
        this.oSelf.innerHTML=bdr;

    }

    clearItems() {
        while(this.listItems.length>0) {
            this.listItems.Remove(0);
        }
    }

    // API回傳一個array, 是 name / value pair, 分別指定API回傳的value及name的key名稱
    initDropDownAPI(apiurl, text_name="text", value_name="value") {
        return new Promise(async (resolve, reject) => {
            this.clearItems();
            let fm = new UIForm();
            let ret = await fm.post(apiurl);
            if (ret && ret.status==200) {
                var retdata = JSON.parse(ret.data);  //把一個JSON字串轉換成 JavaScript的數值或是物件
                for (let i = 0; i < retdata.length; i++) {
                    this.listItems.push({
                        text: retdata[i][text_name],
                        value: retdata[i][value_name]
                    });
                }
                resolve(true);
            }
            else {
                resolve(false);
            }
        });
    }

    // 回傳勾選的filter
    getFilterValue() {
        let lst=this.oSelf.getElementsByTagName("INPUT");
        let lstOut=[];
        for(let i=0;i<lst.length;i++) {
            // 勾選全部, 不需要filter
            if (lst[i].type=="checkbox" && lst[i].id.substr(0,4)=="all_" && lst[i].checked) {
                return [];
            }
            if (lst[i].type=="checkbox" && lst[i].id.substr(0,3)=="ck_" && lst[i].checked) {
                lstOut.push(lst[i].value);
            }
        }
        // 全不選, 送空白字串
        if (lstOut.length==0) {
            lstOut.push("");
        }
        return lstOut;
    }

    get Value() {
        return this.getFilterValue();
    }

}

// =======================================================================
// 可以輸入文字filter, 按enter後會成為tag在左邊的元件
// 必須為<div>
// 參數:
//  ev_filterchange : 改奱filter內容時呼叫 
//  max_tags: 搜尋關鍵字上限, 內定為5
class UISearch extends UIBase {
    constructor(obj) {
        super(obj);
        if (this.oSelf.tagName!="DIV")
            console.error("UIDropDownFilter must be <DIV> : id = ", this.oSelf.id);

        if (obj.ev_filterchange) {
            this.ev_filterchange = obj.ev_filterchange;
        }
        if (obj.max_tags)
            this.max_tags = obj.max_tags;
        else
            this.max_tags = 5;

        // 加上class
        this.oSelf.classList.add("w3-pading");
        this.oSelf.classList.add("w3-border");
        this.oSelf.classList.add("w3-border-blue");
        this.oSelf.classList.add("w3-round-large");
        // 加上style
        this.oSelf.style="width:100%;display:inline-flex;height:2.2rem;";
        // input的id要存起來
        this.idTxt = "stxt_"+UIBase.getSeq();
        this.idTags = "txttags_"+UIBase.getSeq();
        this.oSelf.innerHTML=`<span id="${this.idTags}"></span><input class="w3-round-xlarge" type="text" id="${this.idTxt}" placeholder=" 輸入關鍵字按Enter" size="16rem;" style="border:none;outline:none;float:left;margin-top:0.2rem;margin-left:0.4rem;" onkeydown="UISearch.enterText(event, '${this.sId}')">`;
    }

    static enterText(e, selfid) {
        // enter pressed
        if (e.keyCode==13 || e.which==13) {
            let self = UIBase.Obj(selfid);
            let dom_txt = document.getElementById(self.idTxt);
            let dom_tags = document.getElementById(self.idTags);
            // 檢查是否超過max_tags個
            let lst_tags=dom_tags.childNodes;
            if (lst_tags.length>=self.max_tags) {
                alert("關鍵字不能超過"+self.max_tags+"個");
                return true;
            }
            // 檢查是否重覆
            for(let i=0;i<lst_tags.length;i++) {
                if (dom_txt.value==lst_tags[i].value) {
                    alert("關鍵字不能重覆 : "+dom_txt.value);
                    dom_txt.focus();
                    return true;
                }
            }
            
            let newtag = document.createElement("SPAN");
            newtag.id="stag_"+UIBase.getSeq();
            newtag.classList.add("w3-tag");
            newtag.classList.add("w3-grey");
            newtag.classList.add("w3-round-large");
            newtag.style="float:left;margin:0.3rem;white-space: nowrap;";
            newtag.innerHTML=dom_txt.value+`<span class="w3-badge w3-tiny w3-button" style="width: 1rem; padding: 0.01rem; margin-left: 0.2rem; height: 1rem; " onclick="UISearch.removeText('${self.sId}', '${newtag.id}')">×</span>`;
            newtag.value=dom_txt.value;
            dom_tags.appendChild(newtag);
            dom_txt.value="";
            if (self.ev_filterchange)
                self.ev_filterchange(self);
        }
        return true;
    }

    static removeText(selfid, id) {
        let self = UIBase.Obj(selfid);
        let dom_tags = document.getElementById(self.idTags);
        let dom_target = document.getElementById(id);
        dom_tags.removeChild(dom_target);
        if (self.ev_filterchange)
            self.ev_filterchange(self);
    }

    // 回傳輸入的關鍵字
    getFilterValue() {
        let dom_tags = document.getElementById(this.idTags);
        let lstOut=[];
        for(let i=0;i<dom_tags.childNodes.length;i++) {
            lstOut.push(dom_tags.childNodes[i].value);
        }
        return lstOut;
    }

    get Value() {
        return this.getFilterValue();
    }
}

// =======================================================================
// interval: integer of milliseconds
// enable: true or false
// ev_timeout: function()
class UITimer {
    constructor(obj) {
        if (obj.ev_timeout) {
            this.ev_timeout = obj.ev_timeout;
        }
        if (obj.interval) {
            if (Number.isInteger(obj.interval)) {
                this.setInterval(obj.interval);
            }
            else {
                console.error("interval of UITimer must be integer");
            }
        }
        else {
            this.setInterval(0);
        }
        if (obj.enable) {
            if (typeof(obj.enable) == typeof(true)) {
                this.setEnable(obj.enable);
            }
            else {
                console.error("enable of UITimer must be boolean");
            }
        }
        else {
            this.setEnable(false);
        }
    }

    setInterval(v) {
        this.interval = v;
    }

    setEnable(v) {
        this.enable = v;
        if (this.enable==true)
            setTimeout(()=> {
                this.resetTimeout();
            }, this.interval); //this.resetTimeout(this.resetTimeout, this.interval);
    }

    // 直接await等待 t milliseconds, 回傳 false表示參數錯誤
    async wait(t) {
        return new Promise(async (resolve, reject) => {
            if (Number.isInteger(t)) {
                setTimeout(() => {
                    resolve(true);
                }, t);
            }
            else {
                console.error("interval of UITimer must be integer");
                resolve(false);
            }
        });
    }

    //setTimeout() {
    //    if (this.enable && this.interval>0) {
    //        setTimeout(this.interval)
    //    }
    //}

    resetTimeout() {
        if (this.interval>0) {
            if (this.ev_timeout)
                this.ev_timeout();
            if (this.enable)
                setTimeout(()=> {
                    this.resetTimeout();
                }, this.interval);
        }
    }

    getInterval() {
        return this.interval;
    }

    getEnable() {
        return this.enable;
    }

    set Interval(v) {
        this.setInterval(v)
    }
    get Interval() {
        this.getInterval()
    }
    set Enable(v) {
        this.setEnable(v)
    }
    get Enable() {
        this.getEnable()
    }
}
/*
module.exports = {
    UIBase,
    UIPage
};
*/

