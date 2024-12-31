onmessage = (e) => {
    const arrayBuffer = e.data;
    const arraydata = [];
    const blob = new Blob([arrayBuffer], { type: 'image/jpeg' });
    const url = URL.createObjectURL(blob);
    arraydata.push(url, blob.size);
    postMessage(arraydata);
};
